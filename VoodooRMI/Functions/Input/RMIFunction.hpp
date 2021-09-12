/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 */

#ifndef RMIFunction_hpp
#define RMIFunction_hpp

#include <rmi.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <RMIBus.hpp>
#include <Configuration.hpp>
#include <VoodooInputMessages.h>

/*
 *  Wrapper class for functions
 */
class RMIFunction : public IOService {
    OSDeclareDefaultStructors(RMIFunction)
    
public:
    inline bool initData(IOService *provider, rmi_configuration *conf, rmi_function *fn) {
        bus = OSDynamicCast(RMIBus, provider);
        if (bus == nullptr) {
            IOLog("%s: Failed to cast bus", getName());
            return false;
        }
        
        desc.command_base_addr = fn->fd.command_base_addr;
        desc.control_base_addr = fn->fd.control_base_addr;
        desc.data_base_addr = fn->fd.data_base_addr;
        desc.function_number = fn->fd.function_number;
        desc.function_version = fn->fd.function_version;
        desc.interrupt_source_count = fn->fd.interrupt_source_count;
        desc.query_base_addr = fn->fd.query_base_addr;
        
        this->conf = conf;
        irqMask = fn->irq_mask[0];
        irqPos = fn->irq_pos;
        return true;
    }
    
    inline unsigned long getIRQ() {
        return irqMask;
    }
    
    inline unsigned int getIRQPos() {
        return irqPos;
    }
    
private:
    unsigned long irqMask;
    unsigned int irqPos;
protected:
    rmi_function_descriptor desc;
    rmi_configuration *conf;
    RMIBus *bus {nullptr};
};

#endif /* RMIFunction_hpp */
