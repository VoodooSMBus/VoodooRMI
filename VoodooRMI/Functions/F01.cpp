/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F01.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "F01.hpp"

OSDefineMetaClassAndStructors(F01, RMIFunction);
#define super IOService

bool F01::init(OSDictionary *dictionary)
{
    if (!super::init())
        return false;
    
    properties = reinterpret_cast<f01_basic_properties*>(IOMalloc(sizeof(f01_basic_properties)));
    if (!properties)
        return false;

    device_control = reinterpret_cast<f01_device_control*>(IOMalloc(sizeof(f01_device_control)));
    if (!device_control)
        return false;
    
    memset(properties, 0, sizeof(f01_basic_properties));
    memset(device_control, 0, sizeof(f01_device_control));
    
    return true;
}

bool F01::attach(IOService *provider)
{
    int error;
    u16 ctrl_base_addr = fn_descriptor->control_base_addr;
    u8 temp, device_status;

    rmiBus = OSDynamicCast(RMIBus, provider);
    if (!rmiBus) {
        IOLogError("%s Could not get RMIBus instance\n", getName());
        return NULL;
    }
    
    num_of_irq_regs = rmiBus->data->num_of_irq_regs;
    
    /*
     * Set the configured bit and (optionally) other important stuff
     * in the device control register.
     */
    
    error = rmiBus->read(fn_descriptor->control_base_addr,
                         &device_control->ctrl0);
    if (error) {
        IOLogError("Failed to read F01 control: %d\n", error);
        return false;
    }
    
    // Always allow dozing for reduced power draw
    // switch (pdata->power_management.nosleep)
    switch (RMI_REG_STATE_OFF) {
        case RMI_REG_STATE_DEFAULT:
            break;
        case RMI_REG_STATE_OFF:
            device_control->ctrl0 &= ~RMI_F01_CTRL0_NOSLEEP_BIT;
            break;
        case RMI_REG_STATE_ON:
            device_control->ctrl0 |= RMI_F01_CTRL0_NOSLEEP_BIT;
            break;
    }
    
    /*
     * Sleep mode might be set as a hangover from a system crash or
     * reboot without power cycle.  If so, clear it so the sensor
     * is certain to function.
     */
    if ((device_control->ctrl0 & RMI_F01_CTRL0_SLEEP_MODE_MASK) !=
        RMI_SLEEP_MODE_NORMAL) {
        IOLogDebug("WARNING: Non-zero sleep mode found. Clearing...\n");
        device_control->ctrl0 &= ~RMI_F01_CTRL0_SLEEP_MODE_MASK;
    }
    
    device_control->ctrl0 |= RMI_F01_CTRL0_CONFIGURED_BIT;
    
    error = rmiBus->write(fn_descriptor->control_base_addr,
                          &device_control->ctrl0);
    if (error) {
        IOLogError("Failed to write F01 control: %d\n", error);
        return NULL;
    }
    
    /* Dummy read in order to clear irqs */
    error = rmiBus->read(fn_descriptor->data_base_addr + 1, &temp);
    if (error < 0) {
        IOLogError("Failed to read Interrupt Status.\n");
        return false;
    }
    
    error = rmi_f01_read_properties();
    if (error < 0) {
        IOLogError("Failed to read F01 properties.\n");
        return false;
    }
    
    IOLog("Found RMI device, manufacturer: %s, product: %s, fw id: %d\n",
             properties->manufacturer_id == 1 ? "Synaptics" : "unknown",
             properties->product_id, properties->firmware_id);

    /* Advance to interrupt control registers, then skip over them. */
    ctrl_base_addr++;
    ctrl_base_addr += num_of_irq_regs;
    
    /* read control register */
    if (properties->has_adjustable_doze) {
        doze_interval_addr = ctrl_base_addr;
        ctrl_base_addr++;
        
        error = rmiBus->read(doze_interval_addr,
                             &device_control->doze_interval);
        if (error) {
            IOLogError("Failed to read F01 doze interval register: %d\n",
                    error);
            return false;
        }
        
        wakeup_threshold_addr = ctrl_base_addr;
        ctrl_base_addr++;
        
        error = rmiBus->read(wakeup_threshold_addr,
                             &device_control->wakeup_threshold);
        if (error < 0) {
            IOLogError("Failed to read F01 wakeup threshold register: %d\n",
                    error);
            return false;
        }
    }
    
    if (properties->has_lts)
        ctrl_base_addr++;
    
    if (properties->has_adjustable_doze_holdoff) {
        doze_holdoff_addr = ctrl_base_addr;
        ctrl_base_addr++;
        
        error = rmiBus->read(doze_holdoff_addr,
                             &device_control->doze_holdoff);
        if (error) {
            IOLogError("Failed to read F01 doze holdoff register: %d\n",
                    error);
            return false;
        }
    }
    
    error = rmiBus->read(fn_descriptor->data_base_addr, &device_status);
    if (error < 0) {
        IOLogError("Failed to read device status: %d\n", error);
        return false;
    }
    
    if (RMI_F01_STATUS_UNCONFIGURED(device_status)) {
        IOLogError("Device was reset during configuration process, status: %#02x!\n",
                RMI_F01_STATUS_CODE(device_status));
        return false;
    }
        
    publishProps();
    
    return super::attach(provider);
}

bool F01::start(IOService* provider)
{
    int retval = 0;
    
    if(!super::start(provider))
        return false;
    
    retval = rmi_f01_config();
    if (retval < 0) {
        IOLogError("Failed to config F01\n");
        return false;
    }
    
    registerService();
    return true;
}

void F01::stop(IOService *provider)
{
    super::stop(provider);
}

void F01::free()
{
    clearDesc();

    if (properties) IOFree(properties, sizeof(f01_basic_properties));
    if (device_control) IOFree(device_control, sizeof(f01_device_control));
    super::free();
}


void F01::publishProps()
{
    deviceDict = OSDictionary::withCapacity(3);
    if (!deviceDict) return;
    deviceDict->setObject("Doze Interval", OSNumber::withNumber(device_control->doze_interval, 8));
    deviceDict->setObject("Doze Holdoff", OSNumber::withNumber(device_control->doze_holdoff, 8));
    deviceDict->setObject("Wakeup Threshold", OSNumber::withNumber(device_control->wakeup_threshold, 8));

    setProperty("Power Properties", deviceDict);

    propDict = OSDictionary::withCapacity(9);
    if (!propDict) return;
    propDict->setObject("Manufacturer ID", OSNumber::withNumber(properties->manufacturer_id, 8));
    propDict->setObject("Has LTS", OSBoolean::withBoolean(properties->has_lts));
    propDict->setObject("Has Adjustable Doze", OSBoolean::withBoolean(properties->has_adjustable_doze));
    propDict->setObject("Has Adjustable Doze Holdoff", OSBoolean::withBoolean(properties->has_adjustable_doze_holdoff));
    
    OSString* dom = OSString::withCString(properties->dom);
    if (dom)
        propDict->setObject("Date of Manufacture", dom);

    // It's null terminated and u8 is a byte, so I guess it's a string?
    OSString* prodID = OSString::withCString(reinterpret_cast<const char*>(properties->product_id));
    if (prodID)
        propDict->setObject("Product ID", prodID);
    propDict->setObject("Product Info", OSNumber::withNumber(properties->productinfo, 16));
    propDict->setObject("Firmware ID", OSNumber::withNumber(properties->firmware_id, 32));
    propDict->setObject("Package ID", OSNumber::withNumber(properties->package_id, 32));

    setProperty("Device Properties", propDict);
}

int F01::rmi_f01_config()
{
    int error;
    
    error = rmiBus->write(fn_descriptor->control_base_addr,
                          &device_control->ctrl0);
    if (error) {
        IOLogError("Failed to write device_control register: %d\n", error);
        return error;
    }
    
    if (properties->has_adjustable_doze) {
        error = rmiBus->write(doze_interval_addr,
                              &device_control->doze_interval);
        if (error) {
            IOLogError("Failed to write doze interval: %d\n", error);
            return error;
        }
        
        error = rmiBus->blockWrite(wakeup_threshold_addr,
                                &device_control->wakeup_threshold,
                                sizeof(u8));
        if (error) {
            IOLogError("Failed to write wakeup threshold: %d\n",
                    error);
            return error;
        }
    }
    
    if (properties->has_adjustable_doze_holdoff) {
        error = rmiBus->write(doze_holdoff_addr,
                              &device_control->doze_holdoff);
        if (error) {
            IOLogError("Failed to write doze holdoff: %d\n", error);
            return error;
        }
    }
    
    return 0;
}

int F01::rmi_f01_read_properties()
{
    u8 queries[RMI_F01_BASIC_QUERY_LEN];
    int ret;
    int query_offset = fn_descriptor->query_base_addr;
    bool has_ds4_queries = false;
    bool has_query42 = false;
    bool has_sensor_id = false;
    bool has_package_id_query = false;
    bool has_build_id_query = false;
    u16 prod_info_addr;
    u8 ds4_query_len;
    
    ret = rmiBus->readBlock(query_offset,
                         queries, RMI_F01_BASIC_QUERY_LEN);
    if (ret) {
        IOLogError("F01 failed to read device query registers: %d\n", ret);
        return ret;
    }
    
    prod_info_addr = query_offset + 17;
    query_offset += RMI_F01_BASIC_QUERY_LEN;
    
    /* Now parse what we got */
    properties->manufacturer_id = queries[0];
    
    properties->has_lts = queries[1] & RMI_F01_QRY1_HAS_LTS;
    properties->has_adjustable_doze =
        queries[1] & RMI_F01_QRY1_HAS_ADJ_DOZE;
    properties->has_adjustable_doze_holdoff =
        queries[1] & RMI_F01_QRY1_HAS_ADJ_DOZE_HOFF;
    has_query42 = queries[1] & RMI_F01_QRY1_HAS_QUERY42;
    has_sensor_id = queries[1] & RMI_F01_QRY1_HAS_SENSOR_ID;
    
    snprintf(properties->dom, sizeof(properties->dom),
             "20%02d/%02d/%02d",
             queries[5] & RMI_F01_QRY5_YEAR_MASK,
             queries[6] & RMI_F01_QRY6_MONTH_MASK,
             queries[7] & RMI_F01_QRY7_DAY_MASK);
    
    memcpy(properties->product_id, &queries[11],
           RMI_PRODUCT_ID_LENGTH);
    properties->product_id[RMI_PRODUCT_ID_LENGTH] = '\0';
    
    properties->productinfo =
        ((queries[2] & RMI_F01_QRY2_PRODINFO_MASK) << 7) |
        (queries[3] & RMI_F01_QRY2_PRODINFO_MASK);
    
    if (has_sensor_id)
        query_offset++;
    
    if (has_query42) {
        ret = rmiBus->read(query_offset, queries);
        if (ret) {
            IOLogError("Failed to read query 42 register: %d\n", ret);
            return ret;
        }
        
        has_ds4_queries = !!(queries[0] & BIT(0));
        query_offset++;
    }
    
    if (has_ds4_queries) {
        ret = rmiBus->read(query_offset, &ds4_query_len);
        if (ret) {
            IOLogError("Failed to read DS4 queries length: %d\n", ret);
            return ret;
        }
        query_offset++;
        
        if (ds4_query_len > 0) {
            ret = rmiBus->read(query_offset, queries);
            if (ret) {
                IOLogError("Failed to read DS4 queries: %d\n",
                        ret);
                return ret;
            }
            
            has_package_id_query = !!(queries[0] & BIT(0));
            has_build_id_query = !!(queries[0] & BIT(1));
        }
        
        if (has_package_id_query) {
            ret = rmiBus->readBlock(prod_info_addr,
                                 queries, sizeof(__le64));
            if (ret) {
                IOLogError("Failed to read package info: %d\n",
                        ret);
                return ret;
            }
            
            // Truncates in F01.c in Linux as well, no clue why.
            // Casting to remove warning
            properties->package_id = (u32) get_unaligned_le64(queries);
            prod_info_addr++;
        }
        
        if (has_build_id_query) {
            ret = rmiBus->readBlock(prod_info_addr, queries, 3);
            if (ret) {
                IOLogError("Failed to read product info: %d\n",
                        ret);
                return ret;
            }
            
            properties->firmware_id = queries[1] << 8 | queries[0];
            properties->firmware_id += queries[2] * 65536;
        }
    }
    
    return 0;
}

int F01::rmi_f01_suspend()
{
    int error;
    
    old_nosleep = device_control->ctrl0 & RMI_F01_CTRL0_NOSLEEP_BIT;
    device_control->ctrl0 &= ~RMI_F01_CTRL0_NOSLEEP_BIT;
    
    device_control->ctrl0 *= ~RMI_F01_CTRL0_SLEEP_MODE_MASK;
    if ((false) /* device may wakeup = false*/)
        device_control->ctrl0 |= RMI_SLEEP_MODE_RESERVED1;
    else
        device_control->ctrl0 |= RMI_SLEEP_MODE_SENSOR_SLEEP;
    
    error = rmiBus->write(fn_descriptor->control_base_addr,
                          &device_control->ctrl0);
    
    if (error) {
        IOLogError("Failed to write sleep mode: %d.\n", error);
        if (old_nosleep)
            device_control->ctrl0 |= RMI_F01_CTRL0_NOSLEEP_BIT;
        device_control->ctrl0 &= ~RMI_F01_CTRL0_SLEEP_MODE_MASK;
        device_control->ctrl0 |= RMI_SLEEP_MODE_NORMAL;
    }
    
    return error;
}

int F01::rmi_f01_resume()
{
    int error;
    
    if (old_nosleep)
        device_control->ctrl0 |= RMI_F01_CTRL0_NOSLEEP_BIT;
    
    device_control->ctrl0 &= ~RMI_F01_CTRL0_SLEEP_MODE_MASK;
    device_control->ctrl0 |= RMI_SLEEP_MODE_NORMAL;
    
    error = rmiBus->write(fn_descriptor->control_base_addr,
                          &device_control->ctrl0);
    
    if (error)
        IOLogError("Failed to restore normal operation: %d.\n", error);
    
    return error;
}

void F01::rmi_f01_attention()
{
    int error;
    u8 device_status;
    
    error = rmiBus->read(fn_descriptor->data_base_addr, &device_status);
    
    if (error) {
        IOLogError("F01: Failed to read device status: %d.\n", error);
        return;
    }
    
    if (RMI_F01_STATUS_BOOTLOADER(device_status))
        IOLogError("Device in bootloader mode, please update firmware\n");
    
    if (RMI_F01_STATUS_UNCONFIGURED(device_status)) {
        IOLogDebug("Device reset detected.\n");
        // reset
    }
}

IOReturn F01::message(UInt32 type, IOService *provider, void *argument)
{
    int error;
    switch (type) {
        case kHandleRMISuspend:
            error = rmi_f01_suspend();
            if (error) return kIOReturnError;
            break;
        case kHandleRMIResume:
            error = rmi_f01_resume();
            if (error) return kIOReturnError;
            break;
        case kHandleRMIAttention:
            rmi_f01_attention();
            break;
    }
    
    return kIOReturnSuccess;
}
