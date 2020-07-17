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
#include <IOKit/IOTimerEventSource.h>
//#include <libkern/OSMalloc.h>
#include "../Utility/LinuxCompat.h"
#include "VoodooSMBusDeviceNub.hpp"
#include "VoodooI2CDeviceNub.hpp"

#define kIOMessageVoodooSMBusHostNotify iokit_vendor_specific_msg(420)
#define kIOMessageVoodooI2CHostNotify   iokit_vendor_specific_msg(421)

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

    inline virtual bool setInterrupt(bool enable) {return true;};

    inline IOReturn message(UInt32 type, IOService *provider, void *argument = 0) APPLE_KEXT_OVERRIDE {
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
    bool init(OSDictionary *dictionary) APPLE_KEXT_OVERRIDE;
    RMISMBus *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void free() APPLE_KEXT_OVERRIDE;
    
    int readBlock(u16 rmiaddr, u8 *databuff, size_t len) APPLE_KEXT_OVERRIDE;
    int blockWrite(u16 rmiaddr, u8 *buf, size_t len) APPLE_KEXT_OVERRIDE;
    
    inline int reset() APPLE_KEXT_OVERRIDE {
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

#define RMI_MOUSE_REPORT_ID        0x01 /* Mouse emulation Report */
#define RMI_WRITE_REPORT_ID        0x09 /* Output Report */
#define RMI_READ_ADDR_REPORT_ID        0x0a /* Output Report */
#define RMI_READ_DATA_REPORT_ID        0x0b /* Input Report */
#define RMI_ATTN_REPORT_ID        0x0c /* Input Report */
#define RMI_SET_RMI_MODE_REPORT_ID    0x0f /* Feature Report */

#define RMI_PAGE_SELECT_REGISTER 0xff
#define RMI_I2C_PAGE(addr) (((addr) >> 8) & 0xff)

#define INTERRUPT_SIMULATOR_INTERVAL 200
#define INTERRUPT_SIMULATOR_TIMEOUT 5
#define INTERRUPT_SIMULATOR_TIMEOUT_BUSY 2
#define INTERRUPT_SIMULATOR_TIMEOUT_IDLE 50

enum rmi_mode_type {
    RMI_MODE_OFF = 0,
    RMI_MODE_ATTN_REPORTS = 1,
    RMI_MODE_NO_PACKED_ATTN_REPORTS = 2,
};

class RMII2C : public RMITransport {
    OSDeclareDefaultStructors(RMII2C);
    typedef IOService super;

public:
    RMII2C *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService* device) APPLE_KEXT_OVERRIDE;

    int readBlock(u16 rmiaddr, u8 *databuff, size_t len) APPLE_KEXT_OVERRIDE;
    int blockWrite(u16 rmiaddr, u8 *buf, size_t len) APPLE_KEXT_OVERRIDE;
    inline int reset() APPLE_KEXT_OVERRIDE {
        return rmi_set_mode(RMI_MODE_ATTN_REPORTS);
    };
    bool setInterrupt(bool enable) APPLE_KEXT_OVERRIDE;

    void simulateInterrupt(OSObject* owner, IOTimerEventSource* timer);
    void interruptOccured(OSObject* owner, IOInterruptEventSource* src, int intCount);
    void notifyClient();

private:
    IOWorkLoop* work_loop;
    IOCommandGate* command_gate;
    IOTimerEventSource* interrupt_simulator;
    IOInterruptEventSource* interrupt_source;
    void releaseResources();

    bool reading {true};
    bool polling {true};
    IOService *client {nullptr};

    VoodooI2CDeviceNub *device_nub;
    IOLock *page_mutex;
    int page {0};
    
    int rmi_set_page(u8 page);
    int rmi_set_mode(u8 mode);
};

#endif // RMITransport_H
