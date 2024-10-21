/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
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

#define kIOMessageVoodooSMBusHostNotify     iokit_vendor_specific_msg(420)
#define kIOMessageRMI4ResetHandler          iokit_vendor_specific_msg(423)
#define kIOMessageRMI4Sleep                 iokit_vendor_specific_msg(424)
#define kIOMessageRMI4Resume                iokit_vendor_specific_msg(425)
#define RMIBusIdentifier "Synaptics RMI4 Device"
#define RMIBusSupported "RMI4 Supported"

typedef void (*RMIAttentionAction)(OSObject *target, AbsoluteTime timestamp, UInt8 *data, size_t size);

/*
 * read/write/reset APIs can be used before opening. Opening/Closing is needed to recieve interrupts
 */
class RMITransport : public IOService {
    OSDeclareDefaultStructors(RMITransport);
    
public:
    // rmi_read_block
    virtual int readBlock(UInt16 rmiaddr, UInt8 *databuff, size_t len) { return -1; };
    // rmi_block_write
    virtual int blockWrite(UInt16 rmiaddr, UInt8 *buf, size_t len) { return -1; };
    
    virtual int reset() { return 0; };
    
    virtual OSDictionary *createConfig() { return nullptr; };
    
    virtual bool open(IOService *client, IOOptionBits options, RMIAttentionAction action) {
        if (!IOService::open(client, options)) {
            return false;
        }
        
        bus = client;
        interruptAction = action;
        return true;
    }
    
    virtual void close(IOService *client, IOOptionBits options = 0) override {
        bus = nullptr;
        interruptAction = nullptr;
        IOService::close(client);
    }
    
    void handleAttention(AbsoluteTime timestamp, UInt8 *data, size_t size) {
        if (!interruptAction) {
            return;
        }
        
        (*interruptAction)(bus, timestamp, data, size);
    }
    
protected:
    IOService *bus {nullptr};
private:
    RMIAttentionAction interruptAction {nullptr};
};

#endif // RMITransport_H
