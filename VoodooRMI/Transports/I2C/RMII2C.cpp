/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Zhen
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_i2c.c
 * https://github.com/torvalds/linux/blob/master/drivers/hid/hid-rmi.c
 * and code written by Alexandre, CoolStar and Kishor Prins
 * https://github.com/VoodooI2C/VoodooI2CSynaptics
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "RMII2C.hpp"

//OSDefineMetaClassAndStructors(RMITransport, IOService)
OSDefineMetaClassAndStructors(RMII2C, RMITransport)

RMII2C *RMII2C::probe(IOService *provider, SInt32 *score)
{
    int error = 0, attempts = 0;

    OSData *data;
    data = OSDynamicCast(OSData, provider->getProperty("name"));
    IOLog("%s: RMII2C probing %s\n", getName(), data->getBytesNoCopy());

    IOService *service = super::probe(provider, score);
    if(!service) {
        IOLog("%s: Failed to probe provider\n", getName());
        return NULL;
    }

    device_nub = OSDynamicCast(VoodooI2CDeviceNub, provider);
    if (!device_nub) {
        IOLog("%s: Could not cast nub\n", getName());
        return NULL;
    }

    do {
        error = rmi_set_mode(RMI_MODE_ATTN_REPORTS);
        IOLog("%s: Trying to set mode, attempt %d\n", getName(), attempts);
        IOSleep(500);
    } while (error && attempts++ < 5);

    if (error) {
        IOLog("%s: Failed to set mode\n", getName());
        return NULL;
    }

    page_mutex = IOLockAlloc();
    IOLockLock(page_mutex);
    /*
     * Setting the page to zero will (a) make sure the PSR is in a
     * known state, and (b) make sure we can talk to the device.
     */
    error = rmi_set_page(0);
    IOLockUnlock(page_mutex);
    if (error) {
        IOLog("%s: Failed to set page select to 0\n", getName());
        return NULL;
    }
    return this;
}

bool RMII2C::start(IOService *provider)
{
    if(!super::start(provider))
        return false;

    work_loop = getWorkLoop();

    if (!work_loop) {
        IOLog("%s: Could not get work loop\n", getName());
        goto exit;
    }

    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate || (work_loop->addEventSource(command_gate) != kIOReturnSuccess)) {
        IOLog("%s: Could not open command gate\n", getName());
        goto exit;
    }

    /* Implementation of polling */
    interrupt_source = IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &RMII2C::interruptOccured), device_nub, 0);
    if (!interrupt_source) {
        IOLog("%s: Could not get interrupt event source\n, trying to fallback on polling.", getName());
        interrupt_simulator = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &RMII2C::simulateInterrupt));
        if (!interrupt_simulator) {
            IOLog("%s: Could not get timer event source\n", getName());
            goto exit;
        }
        work_loop->addEventSource(interrupt_simulator);
        IOLog("%s: Polling mode initialisation succeeded.", getName());
        setProperty("Interrupt mode", "Polling");
    } else {
        work_loop->addEventSource(interrupt_source);
        IOLog("%s: running on interrupt mode.", getName());
        setProperty("Interrupt mode", "Native");
    }

    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, RMIPowerStates, 2);

    setProperty("VoodooI2CServices Supported", kOSBooleanTrue);
    setProperty(RMIBusSupported, kOSBooleanTrue);
    registerService();
    return true;
exit:
    releaseResources();
    return false;
}

void RMII2C::releaseResources() {
    if (command_gate) {
//        command_gate->disable();
        work_loop->removeEventSource(command_gate);
        OSSafeReleaseNULL(command_gate);
    }

    if (interrupt_source) {
        interrupt_source->disable();
        work_loop->removeEventSource(interrupt_source);
        OSSafeReleaseNULL(interrupt_source);
    }

    if (interrupt_simulator) {
        interrupt_simulator->disable();
        work_loop->removeEventSource(interrupt_simulator);
        OSSafeReleaseNULL(interrupt_simulator);
    }

    OSSafeReleaseNULL(work_loop);

    if (device_nub) {
        if (device_nub->isOpen(this))
            device_nub->close(this);
        device_nub = nullptr;
    }

    IOLockFree(page_mutex);
}

void RMII2C::stop(IOService *device) {
    releaseResources();
    PMstop();
    super::stop(device);
}

int RMII2C::rmi_set_page(u8 page)
{
    // simplified version of rmi_write_report, hid_hw_output_report, i2c_hid_output_report,
    // i2c_hid_output_raw_report, i2c_hid_set_or_send_report and __i2c_hid_command
    u8 writeReport[8] = {
        0x25,  // outputRegister & 0xFF; wOutputRegister
        0x00,  // outputRegister >> 8;
        0x06,  // size & 0xFF
        0x00,  // size >> 8
        RMI_WRITE_REPORT_ID,
        0x01,
        RMI_PAGE_SELECT_REGISTER,
        page };

    if (device_nub->writeI2C(writeReport, sizeof(writeReport)) != kIOReturnSuccess) {
        IOLog("%s: failed to write request output report\n", getName());
        return -1;
    }

    this->page = page;
    return 0;
}

int RMII2C::rmi_set_mode(u8 mode) {
    u8 command[] = {
        0x22, // registerIndex : wCommandRegister
        0x00, // registerIndex+1
        0x3f, // reportID | reportType << 4;
              // reportType: 0x03 for HID_FEATURE_REPORT (kIOHIDReportTypeFeature) ; 0x02 for HID_OUTPUT_REPORT (kIOHIDReportTypeOutput)
        0x03, // hid_set_report_cmd =    { I2C_HID_CMD(0x03) };
        0x0f, // report_id -> RMI_SET_RMI_MODE_REPORT_ID
        0x23, // dataRegister & 0xFF; wDataRegister
        0x00, // dataRegister >> 8;
        0x04, // size & 0xFF; 2 + reportID + buf (reportID excluded)
        0x00, // size >> 8;
        RMI_SET_RMI_MODE_REPORT_ID, // report_id = buf[0];
        mode };

    if (device_nub->writeI2C(command, sizeof(command)) != kIOReturnSuccess)
        return -1;
    else
        return 0;
}

int RMII2C::readBlock(u16 rmiaddr, u8 *databuff, size_t len) {
    int retval = 0;
    u8 writeReport[10] = {
        0x25,  // outputRegister & 0xFF; wOutputRegister
        0x00,  // outputRegister >> 8;
        0x08,  // size & 0xFF; 2 + reportID + buf (reportID excluded)
        0x00,  // size >> 8;
        RMI_READ_ADDR_REPORT_ID,
        0x00,  // /* old 1 byte read count */
        (u8) (rmiaddr & 0xFF),
        (u8) (rmiaddr >> 8),
        (u8) (len & 0xFF),
        (u8) (len >> 8) };

    // maybe no need to strip the I2C (and RMI_READ_DATA_REPORT_ID) header?
    u8 *i2cInput = new u8[len+4];
    memset(databuff, 0, len);

    IOLockLock(page_mutex);
    if (RMI_I2C_PAGE(rmiaddr) != page) {
        retval = rmi_set_page(RMI_I2C_PAGE(rmiaddr));
        if (retval < 0)
            goto exit;
    }

    if (device_nub->writeI2C(writeReport, sizeof(writeReport)) != kIOReturnSuccess) {
        IOLog("%s: failed to read request output report\n", getName());
        retval = -1;
        goto exit;
    }

    if (device_nub->readI2C(i2cInput, len+4) != kIOReturnSuccess) {
        IOLog("%s: failed to read I2C input\n", getName());
        retval = -1;
        goto exit;
    }

    if (i2cInput[2] != RMI_READ_DATA_REPORT_ID) {
        IOLog("%s: RMI_READ_DATA_REPORT_ID mismatch\n", getName());
        retval = -1;
        goto exit;
    }

    memcpy(databuff, i2cInput+4, len);
exit:
//    IOLog("read %zd bytes at %#06x: %d (%*ph)\n", len, rmiaddr, ret, (int)len, databuff);
    delete[] i2cInput;
    IOLockUnlock(page_mutex);
    return retval;
}

int RMII2C::blockWrite(u16 rmiaddr, u8 *buf, size_t len) {
    int retval = 0;
    u8 *writeReport = new u8[len+8] {
        0x25,  // outputRegister & 0xFF; wOutputRegister
        0x00,  // outputRegister >> 8;
        (u8) ((len + 6) & 0xFF),  // size & 0xFF; 2 + reportID + buf (reportID excluded)
        (u8) ((len + 6) >> 8),  // size >> 8;
        RMI_WRITE_REPORT_ID,
        (u8) len,
        (u8) (rmiaddr & 0xFF),
        (u8) (rmiaddr >> 8) };

    IOLockLock(page_mutex);
    if (RMI_I2C_PAGE(rmiaddr) != page) {
        retval = rmi_set_page(RMI_I2C_PAGE(rmiaddr));
        if (retval < 0)
            goto exit;
    }

    memcpy(writeReport+8, buf, len);

    if (device_nub->writeI2C(writeReport, len+8) != kIOReturnSuccess) {
        IOLog("%s: failed to write request output report\n", getName());
        retval = -1;
        goto exit;
    }
    retval = 0;

exit:
//    IOLog("write %zd bytes at %#06x: %d (%*ph)\n", len, rmiaddr, ret, (int)len, buf);
    delete [] writeReport;
    IOLockUnlock(page_mutex);
    return retval;
}

void RMII2C::interruptOccured(OSObject *owner, IOInterruptEventSource *src, int intCount) {
    if (reading || !client)// || !awake)
        return;

    command_gate->attemptAction(OSMemberFunctionCast(IOCommandGate::Action, this, &RMII2C::notifyClient));
}

void RMII2C::notifyClient() {
    // Do we really need it in command gate?
    reading = true;
    messageClient(kIOMessageVoodooI2CHostNotify, client);
    reading = false;
}

void RMII2C::simulateInterrupt(OSObject* owner, IOTimerEventSource* timer) {
    interruptOccured(owner, NULL, 0);
    if (polling)
        interrupt_simulator->setTimeoutMS(INTERRUPT_SIMULATOR_TIMEOUT);
}

bool RMII2C::setInterrupt(bool enable) {
    IOLog("%s: interrupt %d", getName(), enable);
    if (enable) {
        reading = false;
        if (!interrupt_source) {
            interrupt_simulator->setTimeoutMS(INTERRUPT_SIMULATOR_INTERVAL);
            polling = true;
        } else {
            interrupt_source->enable();
        }
    } else {
        reading = true;
        if (!interrupt_source)
            polling = false;
        else
            interrupt_source->disable();
    }

    return true;
}

IOReturn RMII2C::setPowerState(unsigned long powerState, IOService *whatDevice){
    IOLog("%s: powerState %ld", getName(), powerState);
    if (whatDevice != this)
        return kIOReturnInvalid;

    if (powerState == 0)
        setInterrupt(false);
    else
        setInterrupt(true);
    return kIOPMAckImplied;
}
