/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 */

#ifndef RMIFunction_hpp
#define RMIFunction_hpp

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include "RMIBus.hpp"
#include "RMIPowerStates.h"

// macOS kernel/math has absolute value in it. It's only for doubles though
#define abs(x) ((x < 0) ? -(x) : (x))

#define MILLI_TO_NANO 1000000

/*
 * Set the state of a register
 *    DEFAULT - use the default value set by the firmware config
 *    OFF - explicitly disable the register
 *    ON - explicitly enable the register
 */
enum rmi_reg_state {
    RMI_REG_STATE_DEFAULT = 0,
    RMI_REG_STATE_OFF = 1,
    RMI_REG_STATE_ON = 2
};

// Parsed PDT data
struct RmiPdtEntry {
    UInt16 dataAddr;
    UInt16 ctrlAddr;
    UInt16 cmdAddr;
    UInt16 qryAddr;
    UInt8 function;
    UInt8 interruptBits;
    UInt32 irqMask;
};

/*
 *  Wrapper class for functions
 */
class RMIFunction : public IOService {
    OSDeclareDefaultStructors(RMIFunction)
    
public:
    virtual bool init(RmiPdtEntry &pdtEntry);
    virtual bool attach(IOService *provider) override;
    virtual bool start(IOService *provider) override;
    
    bool hasAttnSig(const UInt32 irq) const;
    
    // Methods to override
    // Config happens after start and is where control registers should be set
    virtual IOReturn config() { return kIOReturnSuccess; };
    // Attention is called whenever this function has data. Any input data
    // should be read here.
    virtual void attention() { };
private:
    RmiPdtEntry pdtEntry;
    RMIBus *bus {nullptr};
    
protected:
    
    // Useful functions to talk to RMI4 devicce
    inline const IOService *getVoodooInput() const { return bus->getVoodooInput(); }
    inline void setVoodooInput(IOService *service) { bus->setVoodooInput(service); }
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
