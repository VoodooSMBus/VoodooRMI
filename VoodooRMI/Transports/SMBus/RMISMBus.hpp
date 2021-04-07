/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_smbus.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#ifndef RMISMBus_h
#define RMISMBus_h

#include "RMITransport.hpp"
#include "VoodooSMBusDeviceNub.hpp"

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
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    IOReturn setPowerState(unsigned long whichState, IOService* whatDevice) override;
    void stop(IOService *provider) override;
    void free() override;
    
    int readBlock(u16 rmiaddr, u8 *databuff, size_t len) override;
    int blockWrite(u16 rmiaddr, u8 *buf, size_t len) override;
    
    int reset() override;
private:
    VoodooSMBusDeviceNub *device_nub;
    IOLock *page_mutex;
    IOLock *mapping_table_mutex;
    
    struct mapping_table_entry mapping_table[RMI_SMB2_MAP_SIZE];
    u8 table_index {0};
    
    bool rmiStart();
    int rmi_smb_get_version();
    int rmi_smb_get_command_code(u16 rmiaddr, int bytecount,
                                 bool isread, u8 *commandcode);
};

#endif /* RMISMBus_h */
