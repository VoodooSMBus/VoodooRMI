/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_smbus.c
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_i2c.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#ifndef RMITransport_H
#define RMITransport_H

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
//#include <libkern/OSMalloc.h>
#include "../Utility/LinuxCompat.h"
#include "VoodooSMBusDeviceNub.hpp"

#define kIOMessageVoodooSMBusHostNotify iokit_vendor_specific_msg(420)

// power management
static IOPMPowerState RMIPowerStates[] = {
    {1, 0                , 0, 0           , 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

class RMITransport : public IOService {
    OSDeclareDefaultStructors(RMITransport);
    
public:
    // rmi_read_block
    virtual int readBlock(u16 rmiaddr, u8 *databuff, size_t len) {return 0;};
    // rmi_block_write
    virtual int blockWrite(u16 rmiaddr, u8 *buf, size_t len) {return 0;};
    
    virtual int reset() {return 0;};
    
    inline IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override {
        IOService *client = getClient();
        if (!client) return kIOReturnError;
        
        switch (type) {
            case kIOMessageVoodooSMBusHostNotify:
                return messageClient(kIOMessageVoodooSMBusHostNotify, client);
            default:
                return IOService::message(type, provider, argument);
        }
    };
};


// VoodooSMBus/VoodooSMBusDeviceNub.hpp
#define I2C_CLIENT_HOST_NOTIFY          0x40    /* We want to use I2C host notify */
#define SMB_PROTOCOL_VERSION_ADDRESS    0xfd
#define SMB_MAX_COUNT                   32
#define RMI_SMB2_MAP_SIZE               8 /* 8 entry of 4 bytes each */
#define RMI_SMB2_MAP_FLAGS_WE           0x01

struct mapping_table_entry {
    __le16 rmiaddr;
    u8 readcount;
    u8 flags;
};

class RMISMBus : public RMITransport {
    OSDeclareDefaultStructors(RMISMBus);
    
public:
    bool init(OSDictionary *dictionary) override;
    RMISMBus *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
    
    int readBlock(u16 rmiaddr, u8 *databuff, size_t len) override;
    int blockWrite(u16 rmiaddr, u8 *buf, size_t len) override;
    
    inline int reset() override {
        /*
         * I don't think this does a full reset, as it still seems to retain memory
         * I believe a PS2 reset needs to be done to completely reset the sensor
         */
        return rmi_smb_get_version();
    }
private:
    VoodooSMBusDeviceNub *device_nub;
    IOLock *page_mutex;
    IOLock *mapping_table_mutex;
    
    struct mapping_table_entry mapping_table[RMI_SMB2_MAP_SIZE];
    u8 table_index;
    
    int rmi_smb_get_version();
    int rmi_smb_get_command_code(u16 rmiaddr, int bytecount,
                                 bool isread, u8 *commandcode);
};
//
//class RMII2C : public RMITransport {
//    OSDeclareDefaultStructors(RMII2C);
//};

#endif // RMITransport_H
