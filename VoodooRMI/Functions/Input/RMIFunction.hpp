/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 */

#ifndef RMIFunction_hpp
#define RMIFunction_hpp

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <RMIBus.hpp>
#include <RMIBusPDT.hpp>

/*
 *  Wrapper class for functions
 */
class RMIFunction : public IOService {
    OSDeclareDefaultStructors(RMIFunction)
    
public:
    inline virtual bool init(RmiPdtEntry &pdtEntry) {
        this->pdtEntry = pdtEntry;
        return IOService::init();
    }
    
    inline virtual bool attach(IOService *provider) override {
        bus = OSDynamicCast(RMIBus, provider);
        if (bus == nullptr) {
            IOLog("%s: Failed to cast bus", getName());
            return false;
        }
        
        return IOService::attach(provider);
    }
    
private:
    RmiPdtEntry pdtEntry;
    RMIBus *bus {nullptr};
protected:
    inline const IOService *getVoodooInput() const { return bus->getVoodooInput(); }
    inline const RmiGpioData &getGPIOData() const { return bus->getGPIOData(); }
    inline const RmiConfiguration &getConfiguration() const { return bus->getConfiguration(); }
    inline IOReturn readByte(UInt16 addr, UInt8 *buf) const { return bus->read(addr, buf); }
    inline IOReturn writeByte(UInt16 addr, UInt8 *buf) const { return bus->write(addr, buf); }
    inline IOReturn readBlock(UInt16 addr, UInt8 *buf, size_t size) const {
        return bus->readBlock(addr, buf, size);
    }
    inline IOReturn writeBlock(UInt16 addr, UInt8 *buf, size_t size) const {
        return bus->blockWrite(addr, buf, size);
    }
    
    inline void notify(UInt32 type, void *argument = 0) const { bus->notify(type, argument); }
    
    inline UInt16 getDataAddr() const { return pdtEntry.dataAddr; }
    inline UInt16 getCtrlAddr() const { return pdtEntry.ctrlAddr; }
    inline UInt16 getCmdAddr() const { return pdtEntry.cmdAddr; }
    inline UInt16 getQryAddr() const { return pdtEntry.qryAddr; }
};

#endif /* RMIFunction_hpp */
