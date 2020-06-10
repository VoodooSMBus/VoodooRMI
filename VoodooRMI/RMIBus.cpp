/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Sensor Controller for macOS
 *
 * Copyright (c) 2020 Avery Black
 */

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
#if DEBUG
    IOLog("RMI Bus (DEBUG) Starting up!");
#else
    IOLog("RMI Bus (RELEASE) Starting up!");
#endif // DEBUG
    
    if (!super::probe(provider, score)) {
        IOLogError("IOService said no to probing\n");
        return NULL;
    }
        
    transport = OSDynamicCast(RMITransport, provider);
    
    if (!transport) {
        IOLogError("%s Could not get transport instance\n", getName());
        return NULL;
    }

    if (rmi_driver_probe(this)) {
        IOLogError("Could not probe");
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
    
    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, RMIPowerStates, 2);
    
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
        IOLogError("Interrupt - No data\n");
        return;
    }
    if (!data->f01_container) {
        IOLogError("Interrupt - No F01 Container\n");
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
                messageClient(kHandleRMIAttention, func);
                
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
        case kIOMessageVoodooSMBusHostNotify:
            handleHostNotify();
            return kIOReturnSuccess;
        default:
            return super::message(type, provider);
    }
}

void RMIBus::notify(UInt32 type, unsigned int argument)
{
    OSIterator* iter = OSCollectionIterator::withCollection(functions);
    while(RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject())) {
        switch (type) {
            case kHandleRMIClickpadSet:
            case kHandleRMITrackpoint:
                if (OSDynamicCast(F11, func)) {
                    IOLogDebug("Sending event %u to F11: %u", type, argument);
                    messageClient(type, func, reinterpret_cast<void *>(argument));
                    return;
                }
                break;
        }
    }
}

IOReturn RMIBus::setPowerState(unsigned long whichState, IOService* whatDevice) {
    if (whatDevice != this)
        return kIOPMAckImplied;
    
    if (whichState == 0 && awake) {
        IOLogDebug("Sleep");
        IOReturn error = messageClients(kHandleRMISuspend);
        rmi_driver_clear_irq_bits(this);
        if (!error) awake = false;
    } else if (!awake) {
        IOSleep(1000);
        IOLogDebug("Wakeup");
        transport->reset();
        // c++ lambdas are wack
        // Sensor doesn't wake up if we don't scan property tables
        rmi_scan_pdt(this, NULL, [](RMIBus *rmi_dev,
                                 void *ctx, const struct pdt_entry *pdt) -> int
        {
            IOLogDebug("Function F%X found again", pdt->function_number);
            return 0;
        });
        rmi_driver_set_irq_bits(this);
        IOReturn error = messageClients(kHandleRMIResume);
        if (!error) awake = true;
    }

    return kIOPMAckImplied;
}

void RMIBus::stop(IOService *provider) {
    OSIterator *iter = OSCollectionIterator::withCollection(functions);
    
    PMstop();
    rmi_driver_clear_irq_bits(this);
    
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

int RMIBus::reset()
{
    return transport->reset();
}

int RMIBus::rmi_register_function(rmi_function *fn) {
    RMIFunction * function;
    
    switch(fn->fd.function_number) {
        case 0x01:
            function = OSDynamicCast(RMIFunction, OSTypeAlloc(F01));
            break;
        case 0x03:
            function = OSDynamicCast(RMIFunction, OSTypeAlloc(F03));
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
