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
#include "../Utility/LinuxCompat.h"
#include "../Utility/Logging.h"
#include "../rmi.h"

#define kIOMessageVoodooSMBusHostNotify     iokit_vendor_specific_msg(420)
#define kIOMessageVoodooI2CHostNotify       iokit_vendor_specific_msg(421)
#define kIOMessageVoodooI2CLegacyHostNotify iokit_vendor_specific_msg(422)
#define kIOMessageRMI4ResetHandler          iokit_vendor_specific_msg(423)
#define kIOMessageRMI4Sleep                 iokit_vendor_specific_msg(424)
#define kIOMessageRMI4Resume                iokit_vendor_specific_msg(425)
#define RMIBusIdentifier "Synaptics RMI4 Device"
#define RMIBusSupported "RMI4 Supported"

// power management
static IOPMPowerState RMIPowerStates[] = {
    {1, 0                , 0, 0           , 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

/*
 * read/write/reset APIs can be used before opening. Opening/Closing is needed to recieve interrupts
 */
class RMITransport : public IOService {
    OSDeclareDefaultStructors(RMITransport);
    
public:
    // rmi_read_block
    virtual int readBlock(u16 rmiaddr, u8 *databuff, size_t len) {return 0;};
    // rmi_block_write
    virtual int blockWrite(u16 rmiaddr, u8 *buf, size_t len) {return 0;};
    
    virtual int reset() {return 0;};
    
    /*
     * IMPORTANT: These handleClose/handleOpen must be called. These can be overriden,
     * but said implementation must call the ones below.
     */
    inline virtual void handleClose(IOService *forClient, IOOptionBits options) override {
        OSSafeReleaseNULL(bus);
        IOService::handleClose(forClient, options);
    }
    
    inline virtual bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override {
        if (forClient && forClient->getProperty(RMIBusIdentifier)
            && IOService::handleOpen(forClient, options, arg)) {
            bus = forClient;
            bus->retain();
            
            return true;
        }
        
        return false;
    }
    
protected:
    IOService *bus {nullptr};
};

#endif // RMITransport_H
