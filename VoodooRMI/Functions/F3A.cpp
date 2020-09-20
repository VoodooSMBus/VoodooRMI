//
//  F3A.cpp
//  VoodooRMI
//
//  Created by Avery Black on 9/20/20.
//  Copyright © 2020 1Revenger1. All rights reserved.
//

#include "F3A.hpp"

OSDefineMetaClassAndStructors(F3A, RMIFunction)
#define super IOService

bool F3A::attach(IOService *provider)
{
    if (!super::attach(provider))
        return false;
    
    rmiBus = OSDynamicCast(RMIBus, provider);
    if (!rmiBus) {
        IOLogError("F3A - Could not cast RMIBus");
        return false;
    }
    
    return true;
}

bool F3A::start(IOService *provider)
{
    
    
    registerService();
    return super::start(provider);
}

IOReturn F3A::message(UInt32 type, IOService *provider, void *argument)
{
    u8 reg = 0;
    int error = 0;
    
    switch (type) {
        case kHandleRMIAttention:
            error = rmiBus->readBlock(fn_descriptor->data_base_addr,
                                          &reg, 1);
            
            if (error < 0) {
                IOLogError("Could not read F30 data: 0x%x", error);
            }
            
            IOLogInfo("F3A Attention! DataReg: %u", reg);
            break;
        default:
            return super::message(type, provider, argument);
    }
    
    return kIOReturnSuccess;
}
