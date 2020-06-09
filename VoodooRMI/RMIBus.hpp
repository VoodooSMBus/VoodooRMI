/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Sensor Controller for macOS
 *
 * Copyright (c) 2020 Avery Black
 */

#ifndef RMIBus_h
#define RMIBus_h

class RMIBus;
class RMIFunction;

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOMessage.h>
#include "LinuxCompat.h"
#include "RMITransport.hpp"
#include "rmi.h"
#include "rmi_driver.hpp"

#include <F01.hpp>
#include <F03.hpp>
#include <F11.hpp>
#include <F30.hpp>
#include <F34.hpp>

#define IOLogError(arg...) IOLog("Error: " arg)

#ifdef DEBUG
#define IOLogDebug(arg...) IOLog("Debug: " arg)
#else
#define IOLogDebug(arg...)
#endif // DEBUG

enum {
    kHandleRMIInterrupt = iokit_vendor_specific_msg(2046),
    kHandleRMIClickpadSet = iokit_vendor_specific_msg(2047),
    kHandleRMISuspend = iokit_vendor_specific_msg(2048),
    kHandleRMIResume = iokit_vendor_specific_msg(2049),
};

class RMIBus : public IOService {
    OSDeclareDefaultStructors(RMIBus);
    
public:
    virtual RMIBus * probe(IOService *provider, SInt32 *score) override;
    virtual bool init(OSDictionary *dictionary) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual void free() override;
    IOReturn setPowerState(unsigned long whichState, IOService* whatDevice) override;
    
    inline IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    
    rmi_driver_data *data;
    RMITransport *transport;
    bool awake {true};
    
    // rmi_read
    inline int read(u16 addr, u8 *buf) {
        return transport->read(addr, buf);
    }
    // rmi_read_block
    inline int readBlock(u16 rmiaddr, u8 *databuff, size_t len) {
        return transport->readBlock(rmiaddr, databuff, len);
    }
    // rmi_write
    inline int write(u16 rmiaddr, u8 *buf) {
        return transport->write(rmiaddr, buf);
    }
    // rmi_block_write
    inline int blockWrite(u16 rmiaddr, u8 *buf, size_t len) {
        return transport->blockWrite(rmiaddr, buf, len);
    }
    
    OSSet *functions;
    
    void notify(UInt32 type, unsigned int argument = 0);
    int rmi_register_function(rmi_function* fn);
    int reset();
private:
    
    void handleHostNotify();
};
    
#endif /* RMIBus_h */
