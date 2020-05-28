//
//  RMIBus.c
//  VoodooSMBus
//
//  Created by Avery Black on 4/30/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#include "RMIBus.hpp"

OSDefineMetaClassAndStructors(RMIBus, IOService)
OSDefineMetaClassAndStructors(RMIFunction, IOService)
#define super IOService

bool RMIBus::init(OSDictionary *dictionary) {
    data = reinterpret_cast<rmi_driver_data *>(IOMalloc(sizeof(rmi_driver_data)));
    memset(data, 0, sizeof(rmi_driver_data));
    
    if (!data) return false;
    
    functions = OSSet::withCapacity(5);
    
    data->irq_mutex = IOLockAlloc();
    data->enabled_mutex = IOLockAlloc();
    return super::init(dictionary);
}

RMIBus * RMIBus::probe(IOService *provider, SInt32 *score) {
    IOLog("Probe");
    if (!super::probe(provider, score))
        return NULL;
    
    transport = OSDynamicCast(RMITransport, provider);
    
    if (!transport) {
        IOLog("%s Could not get transport instance\n", getName());
        return NULL;
    }

    if (rmi_driver_probe(this)) {
        IOLog("Could not probe");
        return NULL;
    }
    
    return this;
}

bool RMIBus::start(IOService *provider) {
    if (!super::start(provider))
        return false;
    int retval;
    
    retval = rmi_init_functions(data);
    if (retval)
        goto err;

    retval = rmi_enable_sensor(this);
    if (retval)
        goto err;
    
//    provider->joinPMtree(this);
//    registerPowerDriver(this, , unsigned long numberOfStates);
    
    registerService();
    return true;
err:
    IOLog("Could not start");
    return false;
}

void RMIBus::handleHostNotify()
{
    unsigned long mask, irqStatus, movingMask = 1;
    if (!data) {
        IOLogError("NO DATA\n");
        return;
    }
    if (!data->f01_container) {
        IOLogError("NO F01 CONTAINER\n");
        return;
    }
    
    int error = readBlock(data->f01_container->fd.data_base_addr + 1,
                          reinterpret_cast<u8*>(&irqStatus), data->num_of_irq_regs);
    
    data->irq_status = irqStatus;
    
    if (error < 0){
        IOLogError("Unable to read IRQ\n");
        return;
    }
    
    IOLockLock(data->irq_mutex);
    mask = data->irq_status & data->fn_irq_bits;
    IOLockUnlock(data->irq_mutex);
    
    OSIterator* iter = OSCollectionIterator::withCollection(functions);
    
    while (mask) {
        if (!(mask & movingMask)) {
            mask &= ~movingMask;
            movingMask <<=1;
            continue;
        }
        
        while(RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject())) {
            if (func->getIRQ() & movingMask) {
                messageClient(kHandleRMIInterrupt, func);
                
                mask &= ~movingMask;
                break;
            }
        }
        
        mask &= ~movingMask;
        iter->reset();
        movingMask <<= 1;
    }
    
    OSSafeReleaseNULL(iter);
}

IOReturn RMIBus::message(UInt32 type, IOService *provider, void *argument) {
    switch (type) {
        case kSmbusAlert:
            handleHostNotify();
            return kIOReturnSuccess;
        default:
            return super::message(type, provider);
    }
}

void RMIBus::stop(IOService *provider) {
    PMstop();
    OSIterator *iter = OSCollectionIterator::withCollection(functions);
    
    while (RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject())) {
        func->detach(this);
        func->stop(this);
    }
    
    functions->flushCollection();
    OSSafeReleaseNULL(iter);
    super::stop(provider);
}

void RMIBus::free() {
    rmi_free_function_list(this);
    
    IOLockFree(data->enabled_mutex);
    IOLockFree(data->irq_mutex);
    OSSafeReleaseNULL(functions);
    super::free();
}

int RMIBus::rmi_register_function(rmi_function *fn) {
    RMIFunction * function;
    
    switch(fn->fd.function_number) {
        case 0x01:
            function = OSDynamicCast(RMIFunction, OSTypeAlloc(F01));
            break;
        case 0x11:
            function = OSDynamicCast(RMIFunction, OSTypeAlloc(F11));
            break;
        case 0x30:
            function = OSDynamicCast(RMIFunction, OSTypeAlloc(F30));
            break;
        case 0x34:
            return 0;
            function = OSDynamicCast(RMIFunction, OSTypeAlloc(F34));
            break;
        case 0x54:
            IOLog("F54 not implemented - Debug function\n");
            return 0;
        default:
            IOLogError("Unknown function: %02X - Continuing to load\n", fn->fd.function_number);
            return 0;
    }
    
    if (!function || !function->init()) {

        IOLogError("Could not initialize function: %02X\n", fn->fd.function_number);
        OSSafeReleaseNULL(function);
        return -ENODEV;
    }
    
    // Duplicate to store in function
    rmi_function_descriptor* desc =
        reinterpret_cast<rmi_function_descriptor*>(IOMalloc(sizeof(rmi_function_descriptor)));
    
    desc->command_base_addr = fn->fd.command_base_addr;
    desc->control_base_addr = fn->fd.control_base_addr;
    desc->data_base_addr = fn->fd.data_base_addr;
    desc->function_number = fn->fd.function_number;
    desc->function_version = fn->fd.function_version;
    desc->interrupt_source_count = fn->fd.interrupt_source_count;
    desc->query_base_addr = fn->fd.query_base_addr;
    
    function->setFunctionDesc(desc);
    function->setMask(fn->irq_mask[0]);
    function->setIrqPos(fn->irq_pos);
    
    if (!function->attach(this)) {
        IOLogError("Function %02X could not attach\n", fn->fd.function_number);
        OSSafeReleaseNULL(function);
        return -ENODEV;
    }
    
    functions->setObject(function);
    
    // For some reason we need to free here otherwise unloading doesn't work.
    // It still is retained by the dictionary and kernel.
    // so it's *probably* fine? Freeing in ::stop causes a page fault
    // TODO: Sanity Check (please ;-;)
    OSSafeReleaseNULL(function);
    return 0;
}
