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
    
    u8 query[F3A_QUERY_SIZE];
    memset (query, 0, sizeof(query));
    // My guess is that F3A only has 2 Query registers
    IOReturn status = rmiBus->readBlock(fn_descriptor->query_base_addr, query, F3A_QUERY_SIZE);
    if (status != 0) {
        IOLogError("F3A - Failed to read Query Registers!");
    }
    
    for(int i = 0; i < F3A_QUERY_SIZE; i++) {
        IOLogDebug("F3A QRY Register %u: %x", i, query[i]);
    }
    
    u8 ctrl[F3A_CTRL_SIZE];
    memset (ctrl, 0, sizeof(ctrl));
    // CTRL size is 99% likely to be variable, need to figure out how to figure out length and stuff though
    status = rmiBus->readBlock(fn_descriptor->control_base_addr, ctrl, F3A_CTRL_SIZE);
    if (status != 0) {
        IOLogError("F3A - Failed to read Ctrl Registers!");
    }
    
    for (int i = 0; i < F3A_CTRL_SIZE; i++) {
        IOLogDebug("F3A CTRL Reg %u: %x", i, ctrl[i]);
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
                IOLogError("Could not read F3A data: 0x%x", error);
            }
            
            IOLogInfo("F3A Attention! DataReg: %u", reg);
            break;
        default:
            return super::message(type, provider, argument);
    }
    
    return kIOReturnSuccess;
}
