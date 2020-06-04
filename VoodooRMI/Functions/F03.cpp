/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F03.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "F03.hpp"

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
    
    return true;
}

IOReturn F03::message(UInt32 type, IOService *provider, void *argument)
{
    IOLog("F03 attention\n");
    return kIOReturnSuccess;
}
