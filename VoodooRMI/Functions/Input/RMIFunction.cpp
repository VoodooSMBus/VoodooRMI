/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Function Wrapper
 *
 * Copyright (c) 2021 Avery Black
 */

#include "RMIFunction.hpp"
#include "RMILogging.h"

OSDefineMetaClassAndStructors(RMIFunction, IOService)

bool RMIFunction::init(RmiPdtEntry &pdtEntry) {
    this->pdtEntry = pdtEntry;
    return IOService::init();
}

bool RMIFunction::attach(IOService *provider) {
    bus = OSDynamicCast(RMIBus, provider);
    if (bus == nullptr) {
        IOLog("%s: Failed to cast bus", getName());
        return false;
    }
    
    return IOService::attach(provider);
}

bool RMIFunction::hasAttnSig(const UInt32 irq) const {
    return pdtEntry.irqMask & irq;
}

bool RMIFunction::start(IOService *provider) {
    if (provider == nullptr ||
        !IOService::start(provider)) {
        return false;
    }
    
    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, RMIPowerStates, 2);
    registerService();
    return true;
}

bool RMIFunction::getInputData(UInt8 dest[], size_t destSize, UInt8 *srcData[], size_t *srcSize) {
    if (*srcData) {
        if (*srcSize < destSize) {
            IOLogError("%s Attention size smaller than expected", getName());
            return false;
        }
        
        memcpy(dest, *srcData, destSize);
        (*srcData) += destSize;
        (*srcSize) -= destSize;
        return true;
    }
    
    if (destSize == 1) {
        IOReturn error = readByte(getDataAddr(), dest);
        
        if (error) {
            IOLogError("%s Failed to read device status: %d", getName(), error);
            return false;
        }
    } else {
        IOReturn error = readBlock(getDataAddr(), dest, destSize);
        
        if (error) {
            IOLogError("%s Failed to read block data: %d", getName(), error);
            return false;
        }
    }
    
    return true;
}
