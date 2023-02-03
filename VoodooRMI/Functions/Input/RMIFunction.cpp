/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Function Wrapper
 *
 * Copyright (c) 2021 Avery Black
 */

#include "RMIFunction.hpp"

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
