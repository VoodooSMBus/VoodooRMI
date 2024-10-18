/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Sensor Controller for macOS
 *
 * Copyright (c) 2021 Avery Black
 */

#ifndef RMIBus_h
#define RMIBus_h

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOCommandGate.h>
#include <Availability.h>
#include "RMITransport.hpp"
#include "RMIConfiguration.hpp"

#ifndef __ACIDANTHERA_MAC_SDK
#error "This kext SDK is unsupported. Download from https://github.com/acidanthera/MacKernelSDK"
#error "You can also do 'git clone --depth=1 https://github.com/acidanthera/MacKernelSDK.git'"
#endif

struct RmiPdtEntry;
class F01;
class RMITrackpadFunction;

class RMIBus : public IOService {
    OSDeclareDefaultStructors(RMIBus);
    
public:
    virtual bool init(OSDictionary *dictionary) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual bool willTerminate(IOService *provider, IOOptionBits options) override;
    virtual void free() override;
    
    virtual bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    virtual void handleClose(IOService *forClient, IOOptionBits options) override;
    virtual bool handleIsOpen(const IOService *forClient) const override;
    
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    IOReturn setProperties(OSObject* properties) override;
    
    // rmi_read
    inline int read(UInt16 addr, UInt8 *buf) const {
        return transport->readBlock(addr, buf, 1);
    }
    // rmi_read_block
    inline int readBlock(UInt16 rmiaddr, UInt8 *databuff, size_t len) const {
        return transport->readBlock(rmiaddr, databuff, len);
    }
    // rmi_write
    inline int write(UInt16 rmiaddr, UInt8 *buf) const {
        return transport->blockWrite(rmiaddr, buf, 1);
    }
    // rmi_block_write
    inline int blockWrite(UInt16 rmiaddr, UInt8 *buf, size_t len) const {
        return transport->blockWrite(rmiaddr, buf, len);
    }
    
    inline IOService *getVoodooInput() const {
        return voodooInputInstance;
    }
    
    inline const RmiGpioData &getGPIOData() const {
        return gpio;
    }
    
    inline const RmiConfiguration &getConfiguration() const {
        return conf;
    }
    
    void notify(UInt32 type, void *argument = 0);
private:
    IOWorkLoop *workLoop {nullptr};
    IOCommandGate *commandGate {nullptr};
    IOService *voodooInputInstance {nullptr};
    OSSet *functions {nullptr};
    
    void publishVoodooInputProperties();
    void getGPIOData(OSDictionary *dict);
    void updateConfiguration(OSDictionary *dictionary);
    RmiConfiguration conf {};
    RmiGpioData gpio {};
    
    RMITransport *transport {nullptr};
    RMITrackpadFunction *trackpadFunction {nullptr};
    IOService *trackpointFunction {nullptr};
    F01 *controlFunction {nullptr};

    void handleHostNotify(AbsoluteTime time, UInt8 *data, size_t size);
    
    // IRQ information
    UInt8 irqCount {0};
    UInt32 irqMask {0};
    
    IOReturn rmiScanPdt();
    IOReturn rmiHandlePdtEntry(RmiPdtEntry &entry);
    IOReturn rmiReadPdtEntry(RmiPdtEntry &entry, UInt16 addr);
    
    IOReturn rmiEnableSensor();
    
    void configAllFunctions();
};
    
#endif /* RMIBus_h */
