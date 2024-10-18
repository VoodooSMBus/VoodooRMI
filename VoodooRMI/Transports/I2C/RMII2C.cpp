/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Zhen
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
#include "RMILogging.h"
#include "RMIConfiguration.hpp"
#include "RMIPowerStates.h"

OSDefineMetaClassAndStructors(RMII2C, RMITransport)

RMII2C *RMII2C::probe(IOService *provider, SInt32 *score) {
    int error = 0, attempts = 0;

    if (!super::probe(provider, score)) {
        IOLogError("%s Failed to probe provider", getName());
        return NULL;
    }

    name = provider->getName();
    IOLogDebug("%s::%s probing", getName(), name);

    device_nub = OSDynamicCast(VoodooI2CDeviceNub, provider);
    if (!device_nub) {
        IOLogError("%s::%s Could not cast nub", getName(), name);
        return NULL;
    }

    if (getHIDDescriptorAddress() != kIOReturnSuccess)
        IOLogInfo("%s::%s Could not get HID descriptor address\n", getName(), name);

    if (getHIDDescriptor() != kIOReturnSuccess) {
        IOLogError("%s::%s Could not get valid HID descriptor", getName(), name);
        return NULL;
    }

    if (hdesc.wVendorID != SYNAPTICS_VENDOR_ID) {
        IOLogDebug("%s::%s Skip vendor %x", getName(), name, hdesc.wVendorID);
        return NULL;
    }

    do {
        IOLogDebug("%s::%s Trying to set mode, attempt %d", getName(), name, attempts);
        error = rmi_set_mode(RMI_MODE_ATTN_REPORTS);
        IOSleep(500);
    } while (error < 0 && attempts++ < 5);

    if (error < 0) {
        IOLogError("%s::%s Failed to set mode", getName(), name);
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
        IOLogError("%s::%s Failed to set page select to 0", getName(), name);
        return NULL;
    }
    return this;
}

bool RMII2C::start(IOService *provider) {
    if(!super::start(provider))
        return false;

    work_loop = getWorkLoop();

    if (!work_loop) {
        IOLogError("%s::%s Could not get work loop", getName(), name);
        goto exit;
    }

    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate || (work_loop->addEventSource(command_gate) != kIOReturnSuccess)) {
        IOLogError("%s::%s Could not open command gate", getName(), name);
        goto exit;
    }

    // Try to get interrupt source, fall back to polling though if we can't get an interrupt source
    interrupt_source = IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &RMII2C::interruptOccured), device_nub, 0);
    
    if (interrupt_source) {
        interrupt_source->enablePrimaryInterruptTimestamp(true);
        work_loop->addEventSource(interrupt_source);
    } else {
        IOLogInfo("%s::%s Could not get interrupt event source, falling back to polling", getName(), name);
        interrupt_simulator = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &RMII2C::simulateInterrupt));
        if (!interrupt_simulator) {
            IOLogError("%s::%s Could not get timer event source", getName(), name);
            goto exit;
        }

        work_loop->addEventSource(interrupt_simulator);
    }
    
    inputBuffer = new UInt8[hdesc.wMaxInputLength];
    
    startInterrupt();

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
    if (command_gate) {
        work_loop->removeEventSource(command_gate);
    }

    stopInterrupt();

    if (interrupt_source) {
        work_loop->removeEventSource(interrupt_source);
    }

    if (interrupt_simulator) {
        work_loop->removeEventSource(interrupt_simulator);
    }

    if (device_nub) {
        if (device_nub->isOpen(this))
            device_nub->close(this);
        device_nub = nullptr;
    }
    
    if (inputBuffer) {
        delete inputBuffer;
    }

    OSSafeReleaseNULL(command_gate);
    OSSafeReleaseNULL(interrupt_source);
    OSSafeReleaseNULL(interrupt_simulator);
    OSSafeReleaseNULL(work_loop);

    IOLockFree(page_mutex);
}

void RMII2C::stop(IOService *provider) {
    releaseResources();
    PMstop();
    super::stop(provider);
}

int RMII2C::rmi_set_page(UInt8 page) {
    /*
     * simplified version of rmi_write_report, hid_hw_output_report, i2c_hid_output_report,
     * i2c_hid_output_raw_report, i2c_hid_set_or_send_report and __i2c_hid_command
     */
    UInt8 writeReport[] = {
        (UInt8) (hdesc.wOutputRegister & 0xFF),
        (UInt8) (hdesc.wOutputRegister >> 8),
        0x06,  // size & 0xFF
        0x00,  // size >> 8
        RMI_WRITE_REPORT_ID,
        0x01,
        RMI_PAGE_SELECT_REGISTER,
        page
    };

    if (device_nub->writeI2C(writeReport, sizeof(writeReport)) != kIOReturnSuccess) {
        IOLogError("%s::%s failed to write request output report", getName(), name);
        return -1;
    }

    this->page = page;
    return 0;
}

IOReturn RMII2C::getHIDDescriptorAddress() {
    IOReturn ret;
    OSObject *obj = nullptr;

    ret = device_nub->evaluateDSM(I2C_DSM_HIDG, HIDG_DESC_INDEX, &obj);
    if (ret == kIOReturnSuccess) {
        OSNumber *number = OSDynamicCast(OSNumber, obj);
        if (number != nullptr) {
            wHIDDescRegister = number->unsigned16BitValue();
            setProperty("HIDDescriptorAddress", wHIDDescRegister, 16);
        } else {
            IOLogInfo("%s::%s HID descriptor address invalid\n", getName(), name);
            ret = kIOReturnInvalid;
        }
    } else {
        IOLogInfo("%s::%s unable to parse HID descriptor address\n", getName(), name);
        ret = kIOReturnNotFound;
    }
    if (obj) obj->release();
    return ret;
}

IOReturn RMII2C::getHIDDescriptor() {
    UInt8 command[] = {
        (UInt8) (wHIDDescRegister & 0xFF),
        (UInt8) (wHIDDescRegister >> 8) };

    if (device_nub->writeReadI2C(command, sizeof(command), (UInt8 *)&hdesc, sizeof(i2c_hid_desc)) != kIOReturnSuccess) {
        IOLogError("%s::%s Read descriptor from 0x%02x failed", getName(), name, wHIDDescRegister);
        return kIOReturnError;
    }

    if (hdesc.bcdVersion != 0x0100) {
        IOLogError("%s::%s BCD version %d mismatch", getName(), name, hdesc.bcdVersion);
        return kIOReturnInvalid;
    }
    
    if (hdesc.wHIDDescLength != sizeof(i2c_hid_desc)) {
        IOLogError("%s::%s descriptor length %d mismatch", getName(), name, hdesc.wHIDDescLength);
        return kIOReturnInvalid;
    }

    setProperty("VendorID", hdesc.wVendorID, 16);
    setProperty("ProductID", hdesc.wProductID, 16);
    setProperty("VersionID", hdesc.wVersionID, 16);

    return kIOReturnSuccess;
}

int RMII2C::rmi_set_mode(UInt8 mode) {
    UInt8 command[] = {
        (UInt8) (hdesc.wCommandRegister & 0xFF),
        (UInt8) (hdesc.wCommandRegister >> 8),
        RMI_SET_RMI_MODE_REPORT_ID + (0x3 << 4), // reportID | reportType << 4;
              // reportType: 0x03 for HID_FEATURE_REPORT (kIOHIDReportTypeFeature) ; 0x02 for HID_OUTPUT_REPORT (kIOHIDReportTypeOutput)
        0x03, // hid_set_report_cmd =    { I2C_HID_CMD(0x03) };
        RMI_SET_RMI_MODE_REPORT_ID, // report_id
        (UInt8) (hdesc.wDataRegister & 0xFF),
        (UInt8) (hdesc.wDataRegister >> 8),
        0x04, // size & 0xFF; 2 + reportID + buf (reportID excluded)
        0x00, // size >> 8;
        RMI_SET_RMI_MODE_REPORT_ID, // report_id = buf[0];
        mode
    };

    if (device_nub->writeI2C(command, sizeof(command)) != kIOReturnSuccess)
        return -1;
    
    IOLogDebug("%s::%s mode set", getName(), name);

    return 1;
}

int RMII2C::reset() {
    int retval = rmi_set_mode(RMI_MODE_ATTN_REPORTS);

    if (retval < 0)
        return retval;
    
    ready = true;
    
    // Tell driver to reconfigure
    retval = messageClient(kIOMessageRMI4ResetHandler, bus);

    IOLogInfo("%s::%s reset completed", getName(), name);
    return retval;
};

bool RMII2C::open(IOService *client, IOOptionBits options, RMIAttentionAction action) {
    if (!super::open(client, options, action)) {
        return false;
    }
    
    startInterrupt();
    return true;
}

int RMII2C::readBlock(UInt16 rmiaddr, UInt8 *databuff, size_t len) {
    int retval = 0;

    if (hdesc.wMaxInputLength && (len > hdesc.wMaxInputLength))
        len = hdesc.wMaxInputLength;

    UInt8 writeReport[] = {
        (UInt8) (hdesc.wOutputRegister & 0xFF),
        (UInt8) (hdesc.wOutputRegister >> 8),
        0x08,  // size & 0xFF; 2 + reportID + buf (reportID excluded)
        0x00,  // size >> 8;
        RMI_READ_ADDR_REPORT_ID,
        0x00,  // old 1 byte read count
        (UInt8) (rmiaddr & 0xFF),
        (UInt8) (rmiaddr >> 8),
        (UInt8) (len & 0xFF),
        (UInt8) (len >> 8) };

    UInt8 *i2cInput = new UInt8[len+4];
    memset(databuff, 0, len);

    IOLockLock(page_mutex);
    if (RMI_I2C_PAGE(rmiaddr) != page) {
        retval = rmi_set_page(RMI_I2C_PAGE(rmiaddr));
        if (retval < 0)
            goto exit;
    }

    if (device_nub->writeReadI2C(writeReport, sizeof(writeReport), i2cInput, len+4) != kIOReturnSuccess) {
        IOLogError("%s::%s failed to read I2C input", getName(), name);
        retval = -1;
        goto exit;
    }

    if (i2cInput[2] != RMI_READ_DATA_REPORT_ID) {
        IOLogError("%s::%s RMI_READ_DATA_REPORT_ID mismatch %d", getName(), name, i2cInput[2]);
        if (i2cInput[2] == HID_GENERIC_MOUSE ||
            i2cInput[2] == HID_GENERIC_POINTER) {
            int err = reset();
            if (err < 0) {
                IOLogError("Failed to reset trackpad after report id mismatch!");
            }
        }
        retval = -1;
        goto exit;
    }

    memcpy(databuff, i2cInput+4, len);
exit:
    delete[] i2cInput;
    IOLockUnlock(page_mutex);
    return retval;
}

int RMII2C::blockWrite(UInt16 rmiaddr, UInt8 *buf, size_t len) {
    int retval = 0;

    if (hdesc.wMaxOutputLength && (len + 6 > hdesc.wMaxOutputLength))
        setProperty("InputLength exceed", len);

    UInt8 *writeReport = new UInt8[len+8] {
        (UInt8) (hdesc.wOutputRegister & 0xFF),
        (UInt8) (hdesc.wOutputRegister >> 8),
        (UInt8) ((len + 6) & 0xFF),  // size & 0xFF; 2 + reportID + buf (reportID excluded)
        (UInt8) ((len + 6) >> 8),  // size >> 8;
        RMI_WRITE_REPORT_ID,
        (UInt8) len,
        (UInt8) (rmiaddr & 0xFF),
        (UInt8) (rmiaddr >> 8) };

    IOLockLock(page_mutex);
    if (RMI_I2C_PAGE(rmiaddr) != page) {
        retval = rmi_set_page(RMI_I2C_PAGE(rmiaddr));
        if (retval < 0)
            goto exit;
    }

    memcpy(writeReport+8, buf, len);

    if (device_nub->writeI2C(writeReport, len+8) != kIOReturnSuccess) {
        IOLogError("%s::%s failed to write request output report", getName(), name);
        retval = -1;
        goto exit;
    }
    retval = 0;

exit:
    delete [] writeReport;
    IOLockUnlock(page_mutex);
    return retval;
}

// We are in the workloop (not interrupt context), it's OK to use IOLog, messageClient, etc
void RMII2C::interruptOccured(OSObject *owner, IOInterruptEventSource *src, int intCount) {
    if (!ready || !bus)
        return;
    
    if (device_nub->readI2C(inputBuffer, hdesc.wMaxInputLength) != kIOReturnSuccess) {
        IOLogError("%s::%s Unable to read interrupt data", getName(), name);
        return;
    }
    
    UInt16 size = inputBuffer[0] | (inputBuffer[1] << 8);
    UInt8 reportId = inputBuffer[2];
    
    if (!size || reportId != RMI_ATTN_REPORT_ID) {
        return;
    }
    
    AbsoluteTime timestamp = mach_absolute_time();
    if (src != nullptr) {
        timestamp = src->getPimaryInterruptTimestamp();
    }
    
    handleAttention(timestamp, &inputBuffer[3], size - 3);
}

void RMII2C::simulateInterrupt(OSObject* owner, IOTimerEventSource* timer) {
    interruptOccured(owner, NULL, 0);
    interrupt_simulator->setTimeoutMS(INTERRUPT_SIMULATOR_TIMEOUT);
}

IOReturn RMII2C::setPowerStateGated() {
    if (currentPowerState == 0) {
        messageClient(kIOMessageRMI4Sleep, bus);
        stopInterrupt();
    } else {
        startInterrupt();
        
        // FIXME: Hardcode 1s sleep delay because device will otherwise time out during reconfig
        IOSleep(1000);
        
        int retval = rmi_set_mode(RMI_MODE_ATTN_REPORTS);
        if (retval < 0) {
            IOLogError("Failed to config trackpad!");
            return kIOPMAckImplied;
        }
        
        retval = messageClient(kIOMessageRMI4Resume, bus);
        if (retval < 0) {
            IOLogError("Failed to resume trackpad!");
            return kIOPMAckImplied;
        }
    }
    
    return kIOPMAckImplied;
}

IOReturn RMII2C::setPowerState(unsigned long newPowerState, IOService *whatDevice) {
    IOLogDebug("%s::%s powerState %ld : %s", getName(), name, newPowerState, newPowerState ? "on" : "off");
    
    if (whatDevice != this)
        return kIOReturnInvalid;
    
    if (!bus || currentPowerState == newPowerState)
        return kIOPMAckImplied;
    
    currentPowerState = newPowerState;
    IOReturn ret = command_gate->attemptAction(OSMemberFunctionCast(IOCommandGate::Action, this, &RMII2C::setPowerStateGated));
    
    if (ret) IOLogDebug("%s::%s powerState ret 0x%x", getName(), name, ret);
    return kIOPMAckImplied;
}

void RMII2C::startInterrupt() {
    if (interrupt_simulator) {
        interrupt_simulator->setTimeoutMS(200);
        interrupt_simulator->enable();
    } else if (interrupt_source) {
        interrupt_source->enable();
    }
    ready = true;
}

void RMII2C::stopInterrupt() {
    ready = false;
    if (interrupt_simulator) {
        interrupt_simulator->disable();
    } else if (interrupt_source) {
        interrupt_source->disable();
    }
}

OSDictionary *RMII2C::createConfig() {
    OSDictionary *config = nullptr;
    OSArray *acpiArray = nullptr;
    OSObject *acpiReturn = nullptr;
    
    IOACPIPlatformDevice *acpi_device = OSDynamicCast(IOACPIPlatformDevice, device_nub->getProperty("acpi-device"));
    if (!acpi_device) {
        IOLogError("%s::%s Could not retrieve acpi device", getName(), name);
        return nullptr;
    };

    if (acpi_device->evaluateObject("RCFG", &acpiReturn) != kIOReturnSuccess) {
        return nullptr;
    }
    
    acpiArray = OSDynamicCast(OSArray, acpiReturn);
    config = Configuration::mapArrayToDict(acpiArray);
    OSSafeReleaseNULL(acpiReturn);
    
    return config;
}
