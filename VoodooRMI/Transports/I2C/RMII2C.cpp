/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Zhen
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/hid/hid-rmi.c
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_i2c.c (depreciated?)
 * and code written by Alexandre, CoolStar and Kishor Prins
 * https://github.com/VoodooI2C/VoodooI2CSynaptics
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "RMII2C.hpp"

OSDefineMetaClassAndStructors(RMII2C, RMITransport)

RMII2C *RMII2C::probe(IOService *provider, SInt32 *score) {
    int error = 0, attempts = 0;

    if (!super::probe(provider, score)) {
        IOLog("%s Failed to probe provider\n", getName());
        return NULL;
    }

    name = provider->getName();
    IOLog("%s::%s probing\n", getName(), name);

    OSBoolean *isLegacy= OSDynamicCast(OSBoolean, getProperty("Legacy"));
    if (isLegacy == nullptr) {
        IOLog("%s::%s Legacy mode not set, default to false", getName(), name);
    } else if (isLegacy->getValue()) {
        reportMode = RMI_MODE_ATTN_REPORTS;
        IOLog("%s::%s running in legacy mode", getName(), name);
    }

    device_nub = OSDynamicCast(VoodooI2CDeviceNub, provider);
    if (!device_nub) {
        IOLog("%s::%s Could not cast nub\n", getName(), name);
        return NULL;
    }

    acpi_device = (OSDynamicCast(IOACPIPlatformDevice, provider->getProperty("acpi-device")));
    if (!acpi_device) {
        IOLog("%s::%s Could not found acpi device\n", getName(), name);
    } else {
        // Sometimes an I2C HID will have power state methods, lets turn it on in case
        acpi_device->evaluateObject("_PS0");
        if (getHIDDescriptorAddress() != kIOReturnSuccess)
            IOLog("%s::%s Could not get HID descriptor address\n", getName(), name);
    }

    do {
#if DEBUG
        IOLog("%s::%s Trying to set mode, attempt %d\n", getName(), name, attempts);
#endif //DEBUG
        error = rmi_set_mode(reportMode);
        IOSleep(500);
    } while (error < 0 && attempts++ < 5);

    if (error < 0) {
        IOLog("%s::%s Failed to set mode\n", getName(), name);
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
        IOLog("%s::%s Failed to set page select to 0\n", getName(), name);
        return NULL;
    }
    return this;
}

bool RMII2C::start(IOService *provider) {
    if(!super::start(provider))
        return false;

    work_loop = getWorkLoop();

    if (!work_loop) {
        IOLog("%s::%s Could not get work loop\n", getName(), name);
        goto exit;
    }

    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate || (work_loop->addEventSource(command_gate) != kIOReturnSuccess)) {
        IOLog("%s::%s Could not open command gate\n", getName(), name);
        goto exit;
    }

    /* Implementation of polling */
    interrupt_source = IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &RMII2C::interruptOccured), device_nub, 0);
    if (!interrupt_source) {
        IOLog("%s::%s Could not get interrupt event source, trying to fallback on polling.", getName(), name);
        interrupt_simulator = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &RMII2C::simulateInterrupt));
        if (!interrupt_simulator) {
            IOLog("%s::%s Could not get timer event source\n", getName(), name);
            goto exit;
        }
    }

    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, RMIPowerStates, 2);

    setProperty("Interrupt mode", (!interrupt_source) ? "Polling" : "Pinned");
    setProperty("VoodooI2CServices Supported", kOSBooleanTrue);
    setProperty(RMIBusSupported, kOSBooleanTrue);
    registerService();
    return true;
exit:
    releaseResources();
    return false;
}

void RMII2C::releaseResources() {
    if (command_gate)
        work_loop->removeEventSource(command_gate);
    OSSafeReleaseNULL(command_gate);

    stopInterrupt();
    OSSafeReleaseNULL(interrupt_source);
    OSSafeReleaseNULL(interrupt_simulator);
    OSSafeReleaseNULL(work_loop);

    if (device_nub) {
        if (device_nub->isOpen(this))
            device_nub->close(this);
        device_nub = nullptr;
    }
    OSSafeReleaseNULL(acpi_device);

    IOLockFree(page_mutex);
}

void RMII2C::stop(IOService *device) {
    releaseResources();
    PMstop();
    super::stop(device);
}

int RMII2C::rmi_set_page(u8 page) {
    /*
     * simplified version of rmi_write_report, hid_hw_output_report, i2c_hid_output_report,
     * i2c_hid_output_raw_report, i2c_hid_set_or_send_report and __i2c_hid_command
     */
    u8 writeReport[] = {
        HID_OUTPUT_REGISTER,  // outputRegister & 0xFF; wOutputRegister
        HID_OUTPUT_REGISTER >> 8,  // outputRegister >> 8;
        0x06,  // size & 0xFF
        0x00,  // size >> 8
        RMI_WRITE_REPORT_ID,
        0x01,
        RMI_PAGE_SELECT_REGISTER,
        page };

    if (device_nub->writeI2C(writeReport, sizeof(writeReport)) != kIOReturnSuccess) {
        IOLog("%s::%s failed to write request output report\n", getName(), name);
        return -1;
    }

    this->page = page;
    return 0;
}

IOReturn RMII2C::getHIDDescriptorAddress() {
    uuid_t guid;
    uuid_parse(HID_I2C_DSM_HIDG, guid);

    // convert to mixed-endian
    *(uint32_t *)guid = OSSwapInt32(*(uint32_t *)guid);
    *((uint16_t *)guid + 2) = OSSwapInt16(*((uint16_t *)guid + 2));
    *((uint16_t *)guid + 3) = OSSwapInt16(*((uint16_t *)guid + 3));

    UInt32 result;
    OSObject *params[4] = {
        OSData::withBytes(guid, 16),
        OSNumber::withNumber(0x1, 8),
        OSNumber::withNumber(0x1, 8),
        OSNumber::withNumber((UInt32)0x0, 8)
    };

    if (acpi_device->evaluateInteger("_DSM", &result, params, 4) != kIOReturnSuccess && acpi_device->evaluateInteger("XDSM", &result, params, 4) != kIOReturnSuccess) {
        IOLog("%s::%s Could not find suitable _DSM or XDSM method in ACPI tables\n", getName(), name);
        return kIOReturnNotFound;
    }

    setProperty("HIDDescriptorAddress", result, 32);

    params[0]->release();
    params[1]->release();
    params[2]->release();
    params[3]->release();

    return kIOReturnSuccess;
}

int RMII2C::rmi_set_mode(u8 mode) {
    u8 command[] = {
        HID_COMMAND_REGISTER, // registerIndex : wCommandRegister
        HID_COMMAND_REGISTER >> 8, // registerIndex+1
        0x3f, // reportID | reportType << 4;
              // reportType: 0x03 for HID_FEATURE_REPORT (kIOHIDReportTypeFeature) ; 0x02 for HID_OUTPUT_REPORT (kIOHIDReportTypeOutput)
        0x03, // hid_set_report_cmd =    { I2C_HID_CMD(0x03) };
        RMI_SET_RMI_MODE_REPORT_ID, // report_id -> RMI_SET_RMI_MODE_REPORT_ID
        HID_DATA_REGISTER, // dataRegister & 0xFF; wDataRegister
        HID_DATA_REGISTER >> 8, // dataRegister >> 8;
        0x04, // size & 0xFF; 2 + reportID + buf (reportID excluded)
        0x00, // size >> 8;
        RMI_SET_RMI_MODE_REPORT_ID, // report_id = buf[0];
        mode };

    if (device_nub->writeI2C(command, sizeof(command)) != kIOReturnSuccess)
        return -1;

    IOLog("%s::%s reset completed", getName(), name);
    return 1;
}

int RMII2C::reset() {
    int retval = rmi_set_mode(reportMode);

    if (retval >= 0)
        ready = true;

    return retval;
};

bool RMII2C::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    if (forClient && forClient->getProperty(RMIBusIdentifier)) {
        bus = forClient;
        bus->retain();

        startInterrupt();
        return true;
    }

    return IOService::handleOpen(forClient, options, arg);
}

int RMII2C::readBlock(u16 rmiaddr, u8 *databuff, size_t len) {
    int retval = 0;
    u8 writeReport[] = {
        HID_OUTPUT_REGISTER,  // outputRegister & 0xFF; wOutputRegister
        HID_OUTPUT_REGISTER >> 8,  // outputRegister >> 8;
        0x08,  // size & 0xFF; 2 + reportID + buf (reportID excluded)
        0x00,  // size >> 8;
        RMI_READ_ADDR_REPORT_ID,
        0x00,  // /* old 1 byte read count */
        (u8) (rmiaddr & 0xFF),
        (u8) (rmiaddr >> 8),
        (u8) (len & 0xFF),
        (u8) (len >> 8) };

    u8 *i2cInput = new u8[len+4];
    memset(databuff, 0, len);

    IOLockLock(page_mutex);
    if (RMI_I2C_PAGE(rmiaddr) != page) {
        retval = rmi_set_page(RMI_I2C_PAGE(rmiaddr));
        if (retval < 0)
            goto exit;
    }

    if (device_nub->writeReadI2C(writeReport, sizeof(writeReport), i2cInput, len+4) != kIOReturnSuccess) {
        IOLog("%s::%s failed to read I2C input\n", getName(), name);
        retval = -1;
        goto exit;
    }

    if (i2cInput[2] != RMI_READ_DATA_REPORT_ID) {
        IOLog("%s::%s RMI_READ_DATA_REPORT_ID mismatch %d\n", getName(), name, i2cInput[2]);
        retval = -1;
        goto exit;
    }

    memcpy(databuff, i2cInput+4, len);
exit:
    delete[] i2cInput;
    IOLockUnlock(page_mutex);
    return retval;
}

int RMII2C::blockWrite(u16 rmiaddr, u8 *buf, size_t len) {
    int retval = 0;
    u8 *writeReport = new u8[len+8] {
        HID_OUTPUT_REGISTER,  // outputRegister & 0xFF; wOutputRegister
        HID_OUTPUT_REGISTER >> 8,  // outputRegister >> 8;
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
        IOLog("%s::%s failed to write request output report\n", getName(), name);
        retval = -1;
        goto exit;
    }
    retval = 0;

exit:
    delete [] writeReport;
    IOLockUnlock(page_mutex);
    return retval;
}

void RMII2C::interruptOccured(OSObject *owner, IOInterruptEventSource *src, int intCount) {
    if (!ready || !bus)
        return;

    command_gate->attemptAction(OSMemberFunctionCast(IOCommandGate::Action, this, &RMII2C::notifyClient));
}

void RMII2C::notifyClient() {
    messageClient(reportMode == RMI_MODE_ATTN_REPORTS ? kIOMessageVoodooI2CLegacyHostNotify : kIOMessageVoodooI2CHostNotify, bus);
}

void RMII2C::simulateInterrupt(OSObject* owner, IOTimerEventSource* timer) {
    interruptOccured(owner, NULL, 0);
    interrupt_simulator->setTimeoutMS(INTERRUPT_SIMULATOR_TIMEOUT);
}

IOReturn RMII2C::setPowerState(unsigned long powerState, IOService *whatDevice){
    IOLog("%s::%s powerState %ld : %s", getName(), name, powerState, powerState ? "on" : "off");
    if (!bus)
        return kIOPMAckImplied;
    if (whatDevice != this)
        return kIOReturnInvalid;

    if (powerState == 0)
        stopInterrupt();
    else
        startInterrupt();
    return kIOPMAckImplied;
}

void RMII2C::startInterrupt() {
    if (interrupt_simulator) {
        work_loop->addEventSource(interrupt_simulator);
        interrupt_simulator->setTimeoutMS(200);
        interrupt_simulator->enable();
    } else if (interrupt_source) {
        work_loop->addEventSource(interrupt_source);
        interrupt_source->enable();
    }
}

void RMII2C::stopInterrupt() {
    ready = false;
    if (interrupt_simulator) {
        interrupt_simulator->disable();
        work_loop->removeEventSource(interrupt_simulator);
    } else if (interrupt_source) {
        interrupt_source->disable();
        work_loop->removeEventSource(interrupt_source);
    }
}
