/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F03.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "F03.hpp"

OSDefineMetaClassAndStructors(F03, RMIFunction)
#define super IOService

/*
 * FIXME: PS2 packets still don't give the right responses.
 * Enabling works, but currently it says it didn't work.
 * So this currently just errors through everything and still works
 */

bool F03::init(OSDictionary *dictionary)
{
    if (!super::init())
        return false;
    trackstickMult = Configuration::loadUInt32Configuration(dictionary, "TrackstickMultiplier", DEFAULT_MULT);
    trackstickScrollXMult = Configuration::loadUInt32Configuration(dictionary, "TrackstickScrollMultiplierX", DEFAULT_MULT);
    trackstickScrollYMult = Configuration::loadUInt32Configuration(dictionary, "TrackstickScrollMultiplierY", DEFAULT_MULT);
    trackstickDeadzone = Configuration::loadUInt32Configuration(dictionary, "TrackstickDeadzone", 1);
    
    return true;
}

bool F03::attach(IOService *provider)
{
    u8 bytes_per_device, query1;
    u8 query2[RMI_F03_DEVICE_COUNT * RMI_F03_BYTES_PER_DEVICE];
    size_t query2_len;
    
    rmiBus = OSDynamicCast(RMIBus, provider);
    
    if (!rmiBus) {
        IOLogError("F03: Provider not RMIBus");
        return false;
    }
    
    int error = rmiBus->read(fn_descriptor->query_base_addr, &query1);
    
    if (error < 0) {
        IOLogError("F03: Failed to read query register: %02X\n", error);
        return false;
    }
    
    device_count = query1 & RMI_F03_DEVICE_COUNT;
    bytes_per_device = (query1 >> RMI_F03_BYTES_PER_DEVICE_SHIFT) &
                        RMI_F03_BYTES_PER_DEVICE;
    
    query2_len = device_count * bytes_per_device;
    
    /*
     * The first generation of image sensors don't have a second part to
     * their f03 query, as such we have to set some of these values manually
     */
    if (query2_len < 1) {
        device_count = 1;
        rx_queue_length = 7;
    } else {
        error = rmiBus->readBlock(fn_descriptor->query_base_addr + 1,
                                  query2, query2_len);
        if (error) {
            IOLogError("Failed to read second set of query registers (%d).\n",
                       error);
            return error;
        }
        
        rx_queue_length = query2[0] & RMI_F03_QUEUE_LENGTH;
    }
    
    setProperty("Device Count", device_count, 8);
    setProperty("Bytes Per Device", bytes_per_device, 8);
    
    return IOService::attach(provider);
}

bool F03::start(IOService *provider)
{
    const u8 ob_len = rx_queue_length * RMI_F03_OB_SIZE;
    u8 obs[RMI_F03_QUEUE_LENGTH * RMI_F03_OB_SIZE];
    
    work_loop = reinterpret_cast<IOWorkLoop*>(getWorkLoop());
    if (!work_loop) {
        IOLogError("F03 - Could not get work loop\n");
        return false;
    }
    
    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate || (work_loop->addEventSource(command_gate) != kIOReturnSuccess)) {
        IOLog("%s Could not open command gate\n", getName());
        if (command_gate) command_gate->release();
        OSSafeReleaseNULL(work_loop);
        return false;
    }
    command_gate->enable();
    
    /*
     * Consume any pending data. Some devices like to spam with
     * 0xaa 0x00 announcement which may confuse us as we try to
     * probe the device
     */
    int error = rmiBus->readBlock(fn_descriptor->data_base_addr + RMI_F03_OB_OFFSET,
                                  obs, ob_len);
    if (!error)
        IOLogDebug("F03 - Consumed %*ph (%d) from PS2 guest\n",
                   ob_len, obs, ob_len);
    
    registerService();
    
    u8 param[2] = {0};

    // Sending these are important to make the trackpoint work.
    // But the responses don't work at all and it's voodoo how it actually works
    error = ps2Command(param, MAKE_PS2_CMD( 0, 2, TP_READ_ID));

    u8 param1[2] = { TP_POR };

    error = ps2Command(param1, MAKE_PS2_CMD(1, 2, TP_COMMAND));
//    if (param1[0] != 0xAA || param1[1] != 0x00)
//        IOLogError("Got [%x, %x], should be [0xAA, 0x00]\n", param1[0], param1[1]);

    error = ps2Command(NULL, PSMOUSE_CMD_ENABLE);
    
    index = 0;
    
    if(!publishButtons()) {
        return false;
    }
    
    IOLog("Start finished");
    return super::start(provider);
}

void F03::stop(IOService *provider)
{
    if (command_gate) {
        work_loop->removeEventSource(command_gate);
        command_gate->release();
        command_gate = NULL;
    }
    OSSafeReleaseNULL(work_loop);
    unpublishButtons();
    super::stop(provider);
}

int F03::rmi_f03_pt_write(unsigned char val)
{
    
    int error = rmiBus->write(fn_descriptor->data_base_addr, &val);
    if (error) {
        IOLogError("F03 - Failed to write to F03 TX register (%d).\n", error);
    }
    
    return error;
}

void F03::handlePacketGated(u8 packet)
{
    UInt32 buttons = (databuf[0] & 0x7) | overwrite_buttons;
    SInt32 dx = ((databuf[0] & 0x10) ? 0xffffff00 : 0) | databuf[1];
    SInt32 dy = -(((databuf[0] & 0x20) ? 0xffffff00 : 0) | databuf[2]);
    index = 0;
    
    // The highest dx/dy is lowered by subtracting by trackstickDeadzone.
    // This however does allows values below the deadzone value to still be sent, preserving control in the lower end
    
    dx -= signum(dx) * min(abs(dx), trackstickDeadzone);
    dy -= signum(dy) * min(abs(dy), trackstickDeadzone);
    
    if (dx || dy) {
        // Must multiply first then divide so we don't multiply by zero
        if(buttons & 0x04) {
            buttonDevice->updateScrollwheel((SInt32)((SInt64)-dy * trackstickScrollYMult / DEFAULT_MULT),
                                            (SInt32)((SInt64)-dx * trackstickScrollXMult / DEFAULT_MULT),
                                            0);
        } else {
            buttonDevice->updateRelativePointer((SInt32)((SInt64)dx * trackstickMult / DEFAULT_MULT),
                                                (SInt32)((SInt64)dy * trackstickMult / DEFAULT_MULT),
                                                buttons);
        }

        rmiBus->notify(kHandleRMITrackpoint);
    }

    IOLogDebug("Dx: %d Dy : %d, Buttons: %d", dx, dy, buttons);
}

IOReturn F03::message(UInt32 type, IOService *provider, void *argument)
{
    
    switch (type) {
        case kHandleRMIAttention: {
            const u16 data_addr = fn_descriptor->data_base_addr + RMI_F03_OB_OFFSET;
            const u8 ob_len = rx_queue_length * RMI_F03_OB_SIZE;
            u8 obs[RMI_F03_QUEUE_LENGTH * RMI_F03_OB_SIZE];
            
            int error = rmiBus->readBlock(data_addr, obs, ob_len);
            if (error) {
                IOLogError("F03 - Failed to read output buffers: %d\n", error);
                return kIOReturnError;
            }
            
            for (int i = 0; i < ob_len; i += RMI_F03_OB_SIZE) {
                u8 ob_status = obs[i];
                u8 ob_data = obs[i + RMI_F03_OB_DATA_OFFSET];
                
                if (!(ob_status & RMI_F03_RX_DATA_OFB))
                    continue;
                
                if (ob_status & RMI_F03_OB_FLAG_TIMEOUT) {
                    IOLogDebug("F03 Timeout Flag");
                    return kIOReturnSuccess;
                }
                if (ob_status & RMI_F03_OB_FLAG_PARITY) {
                    IOLogDebug("F03 Parity Flag");
                    return kIOReturnSuccess;
                }
                
                IOLogDebug("F03 - Recieved data over PS2: %x", ob_data);
                if (!cmdcnt && !flags) {
                    // Wait for start of packets
                    if (index == 0 && ((ob_data == PS2_RET_ACK) || !(ob_data & 0x08)))
                        continue;
                    
                    databuf[index++] = ob_data;
                    
                    if (index == 3)
                        handlePacketGated(ob_data);
                }
                
                // ps2_handle_response
                if (cmdcnt)
                    cmdbuf[--cmdcnt] = ob_data;
                
                if (flags & PS2_FLAG_CMD1) {
                    flags &= ~PS2_FLAG_CMD1;
                    if (cmdcnt)
                        command_gate->commandWakeup(&status);
                }
                
                if (flags & PS2_FLAG_ACK) {
                    flags &= ~PS2_FLAG_ACK;
                    command_gate->commandWakeup(&status);
                }
                
                if (!cmdcnt) {
                    flags &= ~PS2_FLAG_CMD;
                    command_gate->commandWakeup(&status);
                }
            }
            break;
        }
        case kHandleRMIResume: {
            u8 param[2] = {0};
            u8 param1[2] = { TP_POR };
            
            int error = ps2Command(param, MAKE_PS2_CMD( 0, 2, TP_READ_ID));
            error = ps2Command(param1, MAKE_PS2_CMD(1, 2, TP_COMMAND));
            if (param1[0] != 0xAA || param1[1] != 0x00)
                IOLogDebug("Got [%x, %x], should be [0xAA, 0x00]\n", param1[0], param1[1]);

            error = ps2Command(NULL, PSMOUSE_CMD_ENABLE);
                
            break;
        }
        case kHandleRMITrackpointButton: {
            // We do not lose any info casting to unsigned int.
            // This message originates in RMIBus::Notify, which sends an unsigned int
            overwrite_buttons = (unsigned int)((intptr_t) argument);
            buttonDevice->updateButtons(overwrite_buttons);
            
            break;
        }
    }

    return kIOReturnSuccess;
}

IOWorkLoop* F03::getWorkLoop()
{
    // Do we have a work loop already?, if so return it NOW.
    if ((vm_address_t) work_loop >> 1)
        return work_loop;
    
    if (OSCompareAndSwap(0, 1, reinterpret_cast<IOWorkLoop*>(&work_loop))) {
        // Construct the workloop and set the cntrlSync variable
        // to whatever the result is and return
        work_loop = IOWorkLoop::workLoop();
    } else {
        while (reinterpret_cast<IOWorkLoop*>(work_loop) == reinterpret_cast<IOWorkLoop*>(1)) {
            // Spin around the cntrlSync variable until the
            // initialization finishes.
            thread_block(0);
        }
    }
    
    return work_loop;
}

int F03::ps2DoSendbyteGated(u8 byte, uint64_t timeout)
{
    AbsoluteTime time;
    
    flags |= PS2_FLAG_ACK;
    
    int error = rmi_f03_pt_write(byte);
    if (error) {
        IOLogDebug("Failed to write to F03 device: %d\n", error);
    }
    nanoseconds_to_absolutetime(timeout, &time);
    IOReturn result = command_gate->commandSleep(&status, time, THREAD_ABORTSAFE);
    status = 0;
    
    if (result) {
        int error = rmi_f03_pt_write(byte);
        if (error) {
            IOLogDebug("Failed to write to F03 device: %d\n", error);
        }
        result = command_gate->commandSleep(&status, time, THREAD_ABORTSAFE);
        status = 0;
    }
    
    if (result == THREAD_TIMED_OUT)
        error = THREAD_TIMED_OUT;
    
    flags &= ~PS2_FLAG_ACK;
    
    return error;
}

int F03::ps2CommandGated(u8 *param, unsigned int *cmd)
{
    unsigned int command = *cmd;
    uint64_t timeout = 500 * 1000000;
    unsigned int send = (command >> 12) & 0xf;
    unsigned int receive = (command >> 8) & 0xf;
    AbsoluteTime time;
    int rc, i;
    IOReturn res;
    u8 send_param[16];
    
    memcpy(send_param, param, send);
    flags = command == PS2_CMD_GETID ? PS2_FLAG_WAITID : 0;
    cmdcnt = receive;
    
    if (receive && param)
        for (i = 0; i < receive;i++)
            cmdbuf[(receive - 1) - i] = param[i];
    
    /* Signal that we are sending the command byte */
    flags |= PS2_FLAG_ACK_CMD;
    
    rc = ps2DoSendbyteGated(command & 0xff, timeout);
    if (rc) {
        goto out_reset_flags;
    }
        
    /* Now we are sending command parameters, if any */
    flags &= ~PS2_FLAG_ACK_CMD;
    
    for (i = 0; i < send; i++) {
        rc = ps2DoSendbyteGated(param[i], timeout);
        if (rc) {
            goto out_reset_flags;
        }
    }
    
    timeout = command == PS2_CMD_RESET_BAT ? 4000 : 500;
    
    flags |= PS2_FLAG_CMD1 | PS2_FLAG_CMD;
    nanoseconds_to_absolutetime(timeout * 1000000, &time);
    res = command_gate->commandSleep(&status, time, THREAD_ABORTSAFE);
    status = 0;
    if (cmdcnt && !(flags & PS2_FLAG_CMD1)) {
        res = command_gate->commandSleep(&status, time, THREAD_ABORTSAFE);
        status = 0;
    }
    
    if (param)
        for (i = 0; i < receive; i++)
            param[i] = cmdbuf[(receive - 1) - i];
    
    if (cmdcnt &&
        (command != PS2_CMD_RESET_BAT || cmdcnt != 1)) {
        // Multibyte commands
        //        rc = -EPROTO;
        goto out_reset_flags;
    }
    
    rc = 0;
    
out_reset_flags:
    flags = 0;
    return rc;
}

int F03::ps2Command(u8 *param, unsigned int command)
{
    return command_gate->attemptAction(OSMemberFunctionCast(IOCommandGate::Action, this, &F03::ps2CommandGated),
                                       param, &command);
}

bool F03::publishButtons()
{
    buttonDevice = OSTypeAlloc(ButtonDevice);
    if (!buttonDevice) {
        IOLogError("No memory to allocate TrackpointDevice instance\n");
        goto trackpoint_exit;
    }
    if (!buttonDevice->init(NULL)) {
        IOLogError("Failed to init TrackpointDevice\n");
        goto trackpoint_exit;
    }
    if (!buttonDevice->attach(this)) {
        IOLogError("Failed to attach TrackpointDevice\n");
        goto trackpoint_exit;
    }
    if (!buttonDevice->start(this)) {
        IOLogError("Failed to start TrackpointDevice \n");
        goto trackpoint_exit;
    }
    
    return true;
trackpoint_exit:
    unpublishButtons();
    return false;
}

void F03::unpublishButtons()
{
    if (buttonDevice) {
        buttonDevice->stop(this);
        OSSafeReleaseNULL(buttonDevice);
    }
}

int F03::signum(int value)
{
    if (value > 0) return 1;
    if (value < 0) return -1;
    return 0;
}
