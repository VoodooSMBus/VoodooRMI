/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F01.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "F01.hpp"
#include <Configuration.hpp>

OSDefineMetaClassAndStructors(F01, RMIFunction);
#define super RMIFunction

bool F01::attach(IOService *provider)
{
    int error;
    u16 ctrl_base_addr = getCtrlAddr();
    u8 temp, device_status;
    
    /*
     * Set the configured bit and (optionally) other important stuff
     * in the device control register.
     */
    
    error = readByte(getCtrlAddr(), &device_control.ctrl0);
    if (error) {
        IOLogError("Failed to read F01 control: %d", error);
        return false;
    }
    
    // Always allow dozing for reduced power draw
    // switch (pdata->power_management.nosleep)
    switch (RMI_REG_STATE_OFF) {
        case RMI_REG_STATE_DEFAULT:
            break;
        case RMI_REG_STATE_OFF:
            device_control.ctrl0 &= ~RMI_F01_CTRL0_NOSLEEP_BIT;
            break;
        case RMI_REG_STATE_ON:
            device_control.ctrl0 |= RMI_F01_CTRL0_NOSLEEP_BIT;
            break;
    }
    
    /*
     * Sleep mode might be set as a hangover from a system crash or
     * reboot without power cycle.  If so, clear it so the sensor
     * is certain to function.
     */
    if ((device_control.ctrl0 & RMI_F01_CTRL0_SLEEP_MODE_MASK) !=
        RMI_SLEEP_MODE_NORMAL) {
        IOLogDebug("WARNING: Non-zero sleep mode found. Clearing...");
        device_control.ctrl0 &= ~RMI_F01_CTRL0_SLEEP_MODE_MASK;
    }
    
    device_control.ctrl0 |= RMI_F01_CTRL0_CONFIGURED_BIT;
    
    error = writeByte(getCtrlAddr(),
                      &device_control.ctrl0);
    if (error) {
        IOLogError("Failed to write F01 control: %d", error);
        return false;
    }
    
    /* Dummy read in order to clear irqs */
    error = readByte(getDataAddr() + 1, &temp);
    if (error < 0) {
        IOLogError("Failed to read Interrupt Status.");
        return false;
    }
    
    error = rmi_f01_read_properties();
    if (error < 0) {
        IOLogError("Failed to read F01 properties.");
        return false;
    }
    
    IOLogInfo("Found RMI4 device, manufacturer: %s, product: %s, fw id: %d",
              properties.manufacturer_id == 1 ? "Synaptics" : "unknown",
              properties.product_id, properties.firmware_id);

    /* Advance to interrupt control registers, then skip over them. */
    ctrl_base_addr++;
    ctrl_base_addr += numIrqRegs;
    
    /* read control register */
    if (properties.has_adjustable_doze) {
        doze_interval_addr = ctrl_base_addr;
        ctrl_base_addr++;
        
        error = readByte(doze_interval_addr,
                         &device_control.doze_interval);
        if (error) {
            IOLogError("Failed to read F01 doze interval register: %d",
                    error);
            return false;
        }
        
        wakeup_threshold_addr = ctrl_base_addr;
        ctrl_base_addr++;
        
        error = readByte(wakeup_threshold_addr,
                         &device_control.wakeup_threshold);
        if (error < 0) {
            IOLogError("Failed to read F01 wakeup threshold register: %d",
                    error);
            return false;
        }
    }
    
    if (properties.has_lts)
        ctrl_base_addr++;
    
    if (properties.has_adjustable_doze_holdoff) {
        doze_holdoff_addr = ctrl_base_addr;
        ctrl_base_addr++;
        
        error = readByte(doze_holdoff_addr,
                          &device_control.doze_holdoff);
        if (error) {
            IOLogError("Failed to read F01 doze holdoff register: %d",
                    error);
            return false;
        }
    }
    
    error = readByte(getDataAddr(), &device_status);
    if (error < 0) {
        IOLogError("Failed to read device status: %d", error);
        return false;
    }
    
    if (RMI_F01_STATUS_UNCONFIGURED(device_status)) {
        IOLogError("Device was reset during configuration process, status: %#02x!",
                RMI_F01_STATUS_CODE(device_status));
        return false;
    }
        
    publishProps();
    
    return super::attach(provider);
}

void F01::publishProps()
{
    OSObject *value;
    OSDictionary *deviceDict = OSDictionary::withCapacity(3);
    if (!deviceDict) return;
    setPropertyNumber(deviceDict, "Doze Interval", device_control.doze_interval, 8);
    setPropertyNumber(deviceDict, "Doze Holdoff", device_control.doze_holdoff, 8);
    setPropertyNumber(deviceDict, "Wakeup Threshold", device_control.wakeup_threshold, 8);
    setProperty("Power Properties", deviceDict);
    deviceDict->release();

    OSDictionary *propDict = OSDictionary::withCapacity(9);
    if (!propDict) return;
    setPropertyNumber(propDict, "Manufacturer ID", properties.manufacturer_id, 8);
    setPropertyBoolean(propDict, "Has LTS", properties.has_lts);
    setPropertyBoolean(propDict, "Has Adjustable Doze", properties.has_adjustable_doze);
    setPropertyBoolean(propDict, "Has Adjustable Doze Holdoff", properties.has_adjustable_doze_holdoff);
    setPropertyString(propDict, "Date of Manufacture", properties.dom);
    setPropertyString(propDict, "Product ID", properties.product_id);
    setPropertyNumber(propDict, "Product Info", properties.productinfo, 16);
    setPropertyNumber(propDict, "Firmware ID", properties.firmware_id, 32);
    setPropertyNumber(propDict, "Package ID", properties.package_id, 32);
    setProperty("Device Properties", propDict);
    propDict->release();
}

IOReturn F01::config()
{
    int error;
    
    error = writeByte(getCtrlAddr(),
                      &device_control.ctrl0);
    if (error) {
        IOLogError("Failed to write device_control register: %d", error);
        return error;
    }
    
    if (properties.has_adjustable_doze) {
        error = writeByte(doze_interval_addr,
                          &device_control.doze_interval);
        if (error) {
            IOLogError("Failed to write doze interval: %d", error);
            return error;
        }
        
        error = writeByte(wakeup_threshold_addr,
                          &device_control.wakeup_threshold);
        if (error) {
            IOLogError("Failed to write wakeup threshold: %d",
                    error);
            return error;
        }
    }
    
    if (properties.has_adjustable_doze_holdoff) {
        error = writeByte(doze_holdoff_addr,
                          &device_control.doze_holdoff);
        if (error) {
            IOLogError("Failed to write doze holdoff: %d", error);
            return error;
        }
    }
    
    return 0;
}

int F01::rmi_f01_read_properties()
{
    u8 queries[RMI_F01_BASIC_QUERY_LEN] = {0};
    int ret;
    int query_offset = getQryAddr();
    bool has_ds4_queries = false;
    bool has_query42 = false;
    bool has_sensor_id = false;
    bool has_package_id_query = false;
    bool has_build_id_query = false;
    u16 prod_info_addr;
    u8 ds4_query_len;
    
    ret = readBlock(query_offset,
                    queries, RMI_F01_BASIC_QUERY_LEN);
    if (ret) {
        IOLogError("F01 failed to read device query registers: %d", ret);
        return ret;
    }
    
    prod_info_addr = query_offset + 17;
    query_offset += RMI_F01_BASIC_QUERY_LEN;
    
    /* Now parse what we got */
    properties.manufacturer_id = queries[0];
    
    properties.has_lts = queries[1] & RMI_F01_QRY1_HAS_LTS;
    properties.has_adjustable_doze =
        queries[1] & RMI_F01_QRY1_HAS_ADJ_DOZE;
    properties.has_adjustable_doze_holdoff =
        queries[1] & RMI_F01_QRY1_HAS_ADJ_DOZE_HOFF;
    has_query42 = queries[1] & RMI_F01_QRY1_HAS_QUERY42;
    has_sensor_id = queries[1] & RMI_F01_QRY1_HAS_SENSOR_ID;
    
    snprintf(properties.dom, sizeof(properties.dom),
             "%02d/%02d/20%02d",
             queries[6] & RMI_F01_QRY7_DAY_MASK,
             queries[5] & RMI_F01_QRY6_MONTH_MASK,
             queries[4] & RMI_F01_QRY5_YEAR_MASK);
    
    memcpy(properties.product_id, &queries[11],
           RMI_PRODUCT_ID_LENGTH);
    properties.product_id[RMI_PRODUCT_ID_LENGTH] = '\0';
    
    properties.productinfo =
        ((queries[2] & RMI_F01_QRY2_PRODINFO_MASK) << 7) |
        (queries[3] & RMI_F01_QRY2_PRODINFO_MASK);
    
    if (has_sensor_id)
        query_offset++;
    
    if (has_query42) {
        ret = readByte(query_offset, queries);
        if (ret) {
            IOLogError("Failed to read query 42 register: %d", ret);
            return ret;
        }
        
        has_ds4_queries = !!(queries[0] & BIT(0));
        query_offset++;
    }
    
    if (has_ds4_queries) {
        ret = readByte(query_offset, &ds4_query_len);
        if (ret) {
            IOLogError("Failed to read DS4 queries length: %d", ret);
            return ret;
        }
        query_offset++;
        
        if (ds4_query_len > 0) {
            ret = readByte(query_offset, queries);
            if (ret) {
                IOLogError("Failed to read DS4 queries: %d",
                        ret);
                return ret;
            }
            
            has_package_id_query = !!(queries[0] & BIT(0));
            has_build_id_query = !!(queries[0] & BIT(1));
        }
        
        if (has_package_id_query) {
            ret = readBlock(prod_info_addr,
                                 queries, sizeof(__le64));
            if (ret) {
                IOLogError("Failed to read package info: %d",
                        ret);
                return ret;
            }
            
            // Truncates in F01.c in Linux as well, no clue why.
            // Casting to remove warning
            properties.package_id = (u32) get_unaligned_le64(queries);
            prod_info_addr++;
        }
        
        if (has_build_id_query) {
            ret = readBlock(prod_info_addr, queries, 3);
            if (ret) {
                IOLogError("Failed to read product info: %d",
                        ret);
                return ret;
            }
            
            properties.firmware_id = queries[1] << 8 | queries[0];
            properties.firmware_id += queries[2] * 65536;
        }
    }
    
    return 0;
}

int F01::rmi_f01_suspend()
{
    int error;
    
    old_nosleep = device_control.ctrl0 & RMI_F01_CTRL0_NOSLEEP_BIT;
    device_control.ctrl0 &= ~RMI_F01_CTRL0_NOSLEEP_BIT;
    
    device_control.ctrl0 &= ~RMI_F01_CTRL0_SLEEP_MODE_MASK;
    if ((false) /* device may wakeup = false*/)
        device_control.ctrl0 |= RMI_SLEEP_MODE_RESERVED1;
    else
        device_control.ctrl0 |= RMI_SLEEP_MODE_SENSOR_SLEEP;
    
    error = writeByte(getCtrlAddr(),
                      &device_control.ctrl0);
    
    if (error) {
        IOLogError("Failed to write sleep mode: %d.", error);
        if (old_nosleep)
            device_control.ctrl0 |= RMI_F01_CTRL0_NOSLEEP_BIT;
        device_control.ctrl0 &= ~RMI_F01_CTRL0_SLEEP_MODE_MASK;
        device_control.ctrl0 |= RMI_SLEEP_MODE_NORMAL;
    }
    
    return error;
}

int F01::rmi_f01_resume()
{
    int error;
    
    if (old_nosleep)
        device_control.ctrl0 |= RMI_F01_CTRL0_NOSLEEP_BIT;
    
    device_control.ctrl0 &= ~RMI_F01_CTRL0_SLEEP_MODE_MASK;
    device_control.ctrl0 |= RMI_SLEEP_MODE_NORMAL;
    
    error = writeByte(getCtrlAddr(), &device_control.ctrl0);
    
    if (error)
        IOLogError("Failed to restore normal operation: %d.", error);
    
    return error;
}

void F01::rmi_f01_attention()
{
    int error;
    u8 device_status = 0;
    
    error = readByte(getDataAddr(), &device_status);
    
    if (error) {
        IOLogError("F01: Failed to read device status: %d", error);
        return;
    }
    
    if (RMI_F01_STATUS_BOOTLOADER(device_status))
        IOLogError("Device in bootloader mode, please update firmware");
    
    if (RMI_F01_STATUS_UNCONFIGURED(device_status)) {
        IOLogError("Device reset detected.");
        // reset
    }
}

IOReturn F01::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) {
    if (whatDevice != this) {
        return kIOPMNoSuchState;
    }
    
    switch (powerStateOrdinal) {
        case RMI_POWER_ON:
            rmi_f01_resume();
            break;
        case RMI_POWER_OFF:
            rmi_f01_suspend();
            break;
        default:
            return kIOPMNoSuchState;
    }
    
    return kIOPMAckImplied;
}

IOReturn F01::message(UInt32 type, IOService *provider, void *argument)
{
    int error = 0;
    switch (type) {
        case kHandleRMIAttention:
            rmi_f01_attention();
            break;
    }
    
    if (error) return kIOReturnError;
    return kIOReturnSuccess;
}

// MARK: RMI4 device IRQs

IOReturn F01::readIRQ(UInt32 &irq) const {
    return readBlock(getDataAddr() + 1,
                      reinterpret_cast<UInt8 *>(&irq),
                      numIrqRegs);
}

IOReturn F01::setIRQs() const {
    IOReturn error;
    UInt32 irqStatus = 0;
    
    // Dummy read in order to clear irqs
    error = readIRQ(irqStatus);
    if (error != kIOReturnSuccess) {
        IOLogError("%s: Failed to read interrupt status!", __func__);
    }
    
    const UInt8 *newMask = reinterpret_cast<const UInt8 *>(&irqMask);
    error = writeBlock(getCtrlAddr() + 1,
                       const_cast<UInt8 *>(newMask),
                       numIrqRegs);
    if (error != kIOReturnSuccess) {
        IOLogError("%s: Failed to change enabled intterupts!", __func__);
    }
    
    return error;
}

IOReturn F01::clearIRQs() const {
    int error = 0;
    UInt32 currentEnabledIRQs;
    
    // Dummy read in order to clear irqs
    error = readIRQ(currentEnabledIRQs);
    if (error != kIOReturnSuccess) {
        IOLogError("%s: Failed to read current IRQs, continuing to clear IRQs", __func__);
    }
    
    // Read current IRQ bits
    error = readBlock(getCtrlAddr() + 1,
                      reinterpret_cast<UInt8 *>(currentEnabledIRQs),
                      numIrqRegs);
    
    if (error != kIOReturnSuccess) {
        IOLogError("%s: Failed to read current enabled IRQs", __func__);
        return error;
    }
    
    // Disable all known IRQs
    currentEnabledIRQs &= ~irqMask;
    
    // Write back new IRQ mask
    error = writeBlock(getCtrlAddr() + 1,
                       reinterpret_cast<UInt8 *>(currentEnabledIRQs),
                       numIrqRegs);
    
    if (error != kIOReturnSuccess) {
        IOLogError("%s: Failed to change enabled interrupts!", __func__);
    }
    
    return error;
}

void F01::setIRQMask(const UInt32 irq, const UInt8 numIrqBits) {
    irqMask = irq;
    numIrqRegs = (numIrqBits + 7) / 8;
}
