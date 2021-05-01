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
#include "rmi.h"
#include "rmi_driver.hpp"
#include "RMI_2D_Sensor.hpp"
#include <Availability.h>

#include <F01.hpp>
#include <F03.hpp>
#include <F11.hpp>
#include <F12.hpp>
#include <F17.hpp>
#include <F30.hpp>
#include <F3A.hpp>

#ifndef __ACIDANTHERA_MAC_SDK
#error "This kext SDK is unsupported. Download from https://github.com/acidanthera/MacKernelSDK"
#error "You can also do 'git clone --depth=1 https://github.com/acidanthera/MacKernelSDK.git'"
#endif

class RMIBus : public IOService {
    OSDeclareDefaultStructors(RMIBus);
    
public:
    virtual RMIBus * probe(IOService *provider, SInt32 *score) override;
    virtual bool init(OSDictionary *dictionary) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual bool willTerminate(IOService *provider, IOOptionBits options) override;
    virtual void free() override;
    
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    IOReturn setProperties(OSObject* properties) override;

    rmi_driver_data *data;
    RMITransport *transport;
    gpio_data gpio;
    
    // rmi_read
    inline int read(u16 addr, u8 *buf) {
        return transport->readBlock(addr, buf, 1);
    }
    // rmi_read_block
    inline int readBlock(u16 rmiaddr, u8 *databuff, size_t len) {
        return transport->readBlock(rmiaddr, databuff, len);
    }
    // rmi_write
    inline int write(u16 rmiaddr, u8 *buf) {
        return transport->blockWrite(rmiaddr, buf, 1);
    }
    // rmi_block_write
    inline int blockWrite(u16 rmiaddr, u8 *buf, size_t len) {
        return transport->blockWrite(rmiaddr, buf, len);
    }
    
    // Return pointer to voodooInput variable as this likely won't init
    // until all other Function services start
    inline IOService **getVoodooInput() {
        return &voodooInputInstance;
    }
    
    inline const gpio_data* getGPIOData() {
        return &gpio;
    }
    
    OSSet *functions;
    
    void notify(UInt32 type, unsigned int argument = 0);
    int rmi_register_function(rmi_function* fn);
    int reset();
private:
    IOWorkLoop *workLoop {nullptr};
    IOCommandGate *commandGate {nullptr};
    IOService *voodooInputInstance {nullptr};
    
    void getGPIOData(OSDictionary *dict);
    void updateConfiguration(OSDictionary *dictionary);
    rmi_configuration conf {};

    void handleHostNotify();
    void handleHostNotifyLegacy();
    void handleReset();
    
    void configAllFunctions();
};
    
#endif /* RMIBus_h */
