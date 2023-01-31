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
#include "Configuration.hpp"
#include <IOKit/acpi/IOACPIPlatformDevice.h>

OSDefineMetaClassAndStructors(RMII2C, RMITransport)

RMII2C *RMII2C::probe(IOService *provider, SInt32 *score) {
    int error = 0, attempts = 0;

    if (!super::probe(provider, score)) {
        IOLogError("%s Failed to probe provider", getName());
        return NULL;
    }

    name = provider->getName();
    IOLogDebug("%s::%s probing", getName(), name);

    hid_interface = OSDynamicCast(IOHIDInterface, provider);
    if (!hid_interface) {
        IOLogError("%s::%s Could not cast nub", getName(), name);
        return NULL;
    }

    if (hid_interface->getVendorID() != SYNAPTICS_VENDOR_ID) {
        IOLogDebug("%s::%s Skip vendor %x", getName(), name, hid_interface->getVendorID());
        return NULL;
    }

    do {
        IOLogDebug("%s::%s Trying to set mode, attempt %d", getName(), name, attempts);
        error = rmi_set_mode(reportMode);
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
    hid_interface->open(this, 0,
                        OSMemberFunctionCast(IOHIDInterface::InterruptReportAction, this, &RMII2C::handleInterruptReport),
                        nullptr);

    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, RMIPowerStates, 2);

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

    if (hid_interface) {
        if (hid_interface->isOpen(this))
            hid_interface->close(this);
        hid_interface = nullptr;
    }

    OSSafeReleaseNULL(command_gate);
    OSSafeReleaseNULL(work_loop);

    IOLockFree(page_mutex);
}

void RMII2C::stop(IOService *provider) {
    releaseResources();
    PMstop();
    super::stop(provider);
}

int RMII2C::rmi_set_page(u8 page) {
    u8 command[] = {RMI_WRITE_REPORT_ID, 0x01, RMI_PAGE_SELECT_REGISTER, page};
    IOMemoryDescriptor *desc = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(command));
    desc->writeBytes(0, command, sizeof(command));
    
    IOReturn ret = hid_interface->setReport(desc, kIOHIDReportTypeOutput, RMI_WRITE_REPORT_ID, 0);
    OSSafeReleaseNULL(desc);
    
    if (ret != kIOReturnSuccess) {
        IOLogError("%s::%s failed to write request output report", getName(), name);
        return -1;
    }

    this->page = page;
    return 0;
}

int RMII2C::rmi_set_mode(u8 mode) {
    u8 command[] = {RMI_SET_RMI_MODE_REPORT_ID, mode};
    IOMemoryDescriptor *desc = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(command));
    desc->writeBytes(0, &command, sizeof(command));
    
    IOReturn ret = hid_interface->setReport(desc, kIOHIDReportTypeFeature, RMI_SET_RMI_MODE_REPORT_ID, 0);
    OSSafeReleaseNULL(desc);
    
    if (ret != kIOReturnSuccess)
        return -1;
    
    IOLogDebug("%s::%s mode set", getName(), name);
    return 1;
}

int RMII2C::reset() {
    int retval = rmi_set_mode(reportMode);

    if (retval < 0)
        return retval;
    
    ready = true;
    
    // Tell driver to reconfigure
    retval = messageClient(kIOMessageRMI4ResetHandler, bus);

    IOLogInfo("%s::%s reset completed", getName(), name);
    return retval;
};

int RMII2C::readBlock(u16 rmiaddr, u8 *databuff, size_t len) {
    return command_gate->attemptAction(OSMemberFunctionCast(IOCommandGate::Action, this, &RMII2C::readBlockGated),
                                       (void *) rmiaddr, databuff, (void *) len);
}

int RMII2C::readBlockGated(u16 rmiaddr, u8 *databuff, size_t len) {
    int retval = 0;
    IOReturn ret;
    AbsoluteTime curTime, deadline;
    
    u8 command[] = {
        RMI_READ_ADDR_REPORT_ID,
        0x00, // Old 1 byte read length
        (u8) (rmiaddr & 0xFF), (u8) (rmiaddr >> 8),
        (u8) (len & 0xFF), (u8) (len >> 8)
    };
    
    IOBufferMemoryDescriptor *report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(command));
    report->writeBytes(0, command, sizeof(command));
    
    memset(databuff, 0, len);
    IOLockLock(page_mutex);
    if (RMI_I2C_PAGE(rmiaddr) != page) {
        retval = rmi_set_page(RMI_I2C_PAGE(rmiaddr));
        if (retval < 0)
            goto exit;
    }
    
    ret = hid_interface->setReport(report, kIOHIDReportTypeOutput, RMI_READ_ADDR_REPORT_ID, 0);
    
    if (ret != kIOReturnSuccess) {
        IOLogError("%s::%s failed to read I2C input", getName(), name);
        retval = -1;
        goto exit;
    }
    
    while (len > 0) {
        clock_get_uptime(&curTime);
        deadline = ADD_ABSOLUTETIME(500 * MILLI_TO_NANO, curTime);
        ret = command_gate->commandSleep(this, deadline, THREAD_UNINT);
        if (ret == kIOReturnTimeout) {
            IOLogError("%s::%s Timeout\n", getName(), name);
            retval = -1;
            break;
        }
        
        if (read_data == nullptr) {
            // No data????
            continue;
        }
        
        read_data->readBytes(0, databuff, read_data->getLength());
        databuff += read_data->getLength();
        read_data = nullptr;
    }

exit:
    OSSafeReleaseNULL(report);
    IOLockUnlock(page_mutex);
    return retval;
}

int RMII2C::blockWrite(u16 rmiaddr, u8 *buf, size_t len) {
    int retval = 0;
    IOBufferMemoryDescriptor *report = nullptr;
    u8 command[] = {
        RMI_WRITE_REPORT_ID,
        (u8) len,
        (u8) (rmiaddr & 0xFF), (u8) (rmiaddr >> 8)
    };

    IOLockLock(page_mutex);
    if (RMI_I2C_PAGE(rmiaddr) != page) {
        retval = rmi_set_page(RMI_I2C_PAGE(rmiaddr));
        if (retval < 0)
            goto exit;
    }
    
    report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, len + sizeof(command));
    report->writeBytes(0, command, sizeof(command));
    report->writeBytes(sizeof(command), buf, len);

    if (hid_interface->setReport(report, kIOHIDReportTypeOutput, RMI_WRITE_REPORT_ID, 0) != kIOReturnSuccess) {
        IOLogError("%s::%s failed to write request output report", getName(), name);
        retval = -1;
    } else {
        retval = 0;
    }

exit:
    OSSafeReleaseNULL(report);
    IOLockUnlock(page_mutex);
    return retval;
}

void RMII2C::handleInterruptReportGated(AbsoluteTime timestamp, IOMemoryDescriptor *report, IOHIDReportType report_type, UInt32 report_id) {
    if (report_id == RMI_READ_DATA_REPORT_ID) {
        read_data = report;
        command_gate->commandWakeup(this);
    } else if (report_id == RMI_ATTN_REPORT_ID) {
        
    } else if (report_id == HID_GENERIC_MOUSE || report_id == HID_GENERIC_POINTER) {
        IOLogError("%s::%s RMI_READ_DATA_REPORT_ID mismatch %d", getName(), name, report_id);
        int err = reset();
        if (err < 0) {
            IOLogError("Failed to reset trackpad after report id mismatch!");
        }
    }
}

void RMII2C::handleInterruptReport(AbsoluteTime timestamp, IOMemoryDescriptor *report, IOHIDReportType report_type, UInt32 report_id) {
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &RMII2C::handleInterruptReportGated),
                            (void *) timestamp, report, (void *) report_type, (void *) report_id);
}

IOReturn RMII2C::setPowerStateGated() {
    if (currentPowerState == 0) {
        messageClient(kIOMessageRMI4Sleep, bus);
    } else {
        // FIXME: Hardcode 1s sleep delay because device will otherwise time out during reconfig
        IOSleep(1000);
        
        int retval = reset();
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

OSDictionary *RMII2C::createConfig() {
    OSDictionary *config = nullptr;
    OSArray *acpiArray = nullptr;
    OSObject *acpiReturn = nullptr;
    
    IOACPIPlatformDevice *acpi_device = OSDynamicCast(IOACPIPlatformDevice, getProperty("acpi-device", gIOServicePlane));
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
