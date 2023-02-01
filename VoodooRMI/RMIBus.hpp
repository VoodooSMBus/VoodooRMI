/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Sensor Controller for macOS
 *
 * Copyright (c) 2021 Avery Black
 */

#ifndef RMIBus_h
#define RMIBus_h

class RMIBus;
class RMIFunction;

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCommandGate.h>
#include "LinuxCompat.h"
#include "Logging.h"
#include "RMITransport.hpp"
#include "rmi_driver.hpp"
#include "RMIBusPDT.hpp"
#include <Availability.h>

#ifndef __ACIDANTHERA_MAC_SDK
#error "This kext SDK is unsupported. Download from https://github.com/acidanthera/MacKernelSDK"
#error "You can also do 'git clone --depth=1 https://github.com/acidanthera/MacKernelSDK.git'"
#endif

class RMIBus : public IOService {
    OSDeclareDefaultStructors(RMIBus);
    
public:
    virtual bool init(OSDictionary *dictionary) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual bool willTerminate(IOService *provider, IOOptionBits options) override;
    virtual void free() override;
    
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    IOReturn setProperties(OSObject* properties) override;

    rmi_driver_data *data;
    RMITransport *transport;
    
    // rmi_read
    inline int read(u16 addr, u8 *buf) const {
        return transport->readBlock(addr, buf, 1);
    }
    // rmi_read_block
    inline int readBlock(u16 rmiaddr, u8 *databuff, size_t len) const {
        return transport->readBlock(rmiaddr, databuff, len);
    }
    // rmi_write
    inline int write(u16 rmiaddr, u8 *buf) const {
        return transport->blockWrite(rmiaddr, buf, 1);
    }
    // rmi_block_write
    inline int blockWrite(u16 rmiaddr, u8 *buf, size_t len) const {
        return transport->blockWrite(rmiaddr, buf, len);
    }
    
    inline IOService *getVoodooInput() const {
        return voodooInputInstance;
    }
    
    inline void setVoodooInput(IOService *service) {
        voodooInputInstance = service;
    }
    
    inline const RmiGpioData &getGPIOData() const {
        return gpio;
    }
    
    inline const RmiConfiguration &getConfiguration() const {
        return conf;
    }
    
    OSSet *functions {nullptr};
    
    void notify(UInt32 type, void *argument = 0) const;
    int reset();
private:
    IOWorkLoop *workLoop {nullptr};
    IOCommandGate *commandGate {nullptr};
    IOService *voodooInputInstance {nullptr};
    
    void getGPIOData(OSDictionary *dict);
    void updateConfiguration(OSDictionary *dictionary);
    RmiConfiguration conf {};
    RmiGpioData gpio {};
    
    IOService *trackpadFunction {nullptr};
    IOService *trackpointFunction {nullptr};

    void handleHostNotify();
    void handleHostNotifyLegacy();
    void handleReset();
    
    // IRQ information
    UInt8 irqCount {0};
    UInt32 irqMask {0};
    
    IOReturn rmiScanPdt();
    IOReturn rmiHandlePdtEntry(RmiPdtEntry &entry);
    IOReturn rmiReadPdtEntry(RmiPdtEntry &entry, UInt16 addr);
    
    void configAllFunctions();
};
    
#endif /* RMIBus_h */
