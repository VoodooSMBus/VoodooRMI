/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Sensor Controller for macOS
 *
 * Copyright (c) 2020 Avery Black
 */

#include "RMIBus.hpp"

OSDefineMetaClassAndStructors(RMIBus, IOService)
OSDefineMetaClassAndStructors(RMIFunction, IOService)
OSDefineMetaClassAndStructors(RMITransport, IOService)
#define super IOService

bool RMIBus::init(OSDictionary *dictionary) {
    if (!super::init(dictionary))
        return false;
    
    data = reinterpret_cast<rmi_driver_data *>(IOMalloc(sizeof(rmi_driver_data)));
    memset(data, 0, sizeof(rmi_driver_data));
    
    if (!data) return false;
    
    functions = OSSet::withCapacity(5);
    
    data->irq_mutex = IOLockAlloc();
    data->enabled_mutex = IOLockAlloc();

    updateConfiguration(OSDynamicCast(OSDictionary, getProperty("Configuration")));
    return true;
}

RMIBus * RMIBus::probe(IOService *provider, SInt32 *score) {
#if DEBUG
    IOLogInfo("RMI Bus (DEBUG) Starting up!");
#else
    IOLogInfo("RMI Bus (RELEASE) Starting up!");
#endif // DEBUG
    
    if (!super::probe(provider, score)) {
        IOLogError("Super said no to probing");
        return NULL;
    }
        
    transport = OSDynamicCast(RMITransport, provider);
    if (!transport) {
        IOLogError("Could not get transport instance");
        return NULL;
    }

    if (rmi_driver_probe(this)) {
        IOLogError("Could not probe");
        return NULL;
    }
    
    return this;
}

bool RMIBus::start(IOService *provider) {
    int retval;
    
    if (!super::start(provider))
        return false;

    if (!(workLoop = IOWorkLoop::workLoop()) ||
        !(commandGate = IOCommandGate::commandGate(this)) ||
        (workLoop->addEventSource(commandGate) != kIOReturnSuccess)) {
        IOLogError("%s Failed to add commandGate", getName());
        return false;
    }

    retval = rmi_init_functions(this, data);
    if (retval)
        goto err;

    retval = rmi_enable_sensor(this);
    if (retval)
        goto err;
    
    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, RMIPowerStates, 2);
    
    registerService();
    setProperty(RMIBusIdentifier, kOSBooleanTrue);
    if (!transport->open(this))
        return false;

    return true;
err:
    IOLogError("Could not start");
    return false;
}

void RMIBus::handleHostNotify()
{
    unsigned long mask, irqStatus, movingMask = 1;
    if (!data) {
        IOLogError("Interrupt - No data");
        return;
    }
    if (!data->f01_container) {
        IOLogError("Interrupt - No F01 Container");
        return;
    }
    
    int error = readBlock(data->f01_container->fd.data_base_addr + 1,
                          reinterpret_cast<u8*>(&irqStatus), data->num_of_irq_regs);
    
    data->irq_status = irqStatus;
    
    if (error < 0){
        IOLogError("Unable to read IRQ");
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
                break;
            }
        }
        
        mask &= ~movingMask;
        iter->reset();
        movingMask <<= 1;
    }
    
    OSSafeReleaseNULL(iter);
}

void RMIBus::handleHostNotifyLegacy()
 {
     if (!data) {
         IOLogError("Interrupt - No data");
         return;
     }
     if (!data->f01_container) {
         IOLogError("Interrupt - No F01 Container");
         return;
     }

     OSIterator* iter = OSCollectionIterator::withCollection(functions);
     while(RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject()))
         messageClient(kHandleRMIAttention, func);
     OSSafeReleaseNULL(iter);
 }

IOReturn RMIBus::message(UInt32 type, IOService *provider, void *argument) {
    switch (type) {
        case kIOMessageVoodooI2CHostNotify:
        case kIOMessageVoodooSMBusHostNotify:
            if (awake)
                handleHostNotify();
            return kIOReturnSuccess;
        case kIOMessageVoodooI2CLegacyHostNotify:
            if (awake)
                handleHostNotifyLegacy();
            return kIOReturnSuccess;
        default:
            return super::message(type, provider);
    }
}

void RMIBus::notify(UInt32 type, unsigned int argument)
{
    // TODO: Maybe make notify not check the type of message, and just send to all?
    // Would save having to write cases for every message that goes through here.
    OSIterator* iter = OSCollectionIterator::withCollection(functions);
    while(RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject())) {
        switch (type) {
            case kHandleRMIClickpadSet:
            case kHandleRMITrackpoint:
                if (OSDynamicCast(F11, func) || OSDynamicCast(F12, func)) {
                    IOLogDebug("Sending event %u to F11/F12: %u", type, argument);
                    messageClient(type, func, reinterpret_cast<void *>(argument));
                    OSSafeReleaseNULL(iter);
                    return;
                }
                break;
            case kHandleRMITrackpointButton:
                if (OSDynamicCast(F03, func)) {
                    IOLogDebug("Sending trackpoint button to F03: %u", argument);
                    messageClient(type, func, reinterpret_cast<void *>(argument));
                }
                break;
        }
    }
    OSSafeReleaseNULL(iter);
}

IOReturn RMIBus::setPowerState(unsigned long whichState, IOService* whatDevice) {
    if (whatDevice != this)
        return kIOPMAckImplied;
    
    if (whichState == 0 && awake) {
        IOLogDebug("Sleep");
        messageClients(kHandleRMISuspend);
        rmi_driver_clear_irq_bits(this);
        awake = false;
    } else if (!awake) {
        IOSleep(1000);
        IOLogDebug("Wakeup");
        if (reset() < 0)
            IOLogError("Could not get SMBus Version on wakeup");
        // c++ lambdas are wack
        // Sensor doesn't wake up if we don't scan property tables
        rmi_scan_pdt(this, NULL, [](RMIBus *rmi_dev,
                                 void *ctx, const struct pdt_entry *pdt) -> int
        {
            IOLogDebug("Function F%X found again", pdt->function_number);
            return 0;
        });
        rmi_driver_set_irq_bits(this);
        messageClients(kHandleRMIResume);
        awake = true;
    }

    return kIOPMAckImplied;
}

void RMIBus::stop(IOService *provider) {
    OSIterator *iter = OSCollectionIterator::withCollection(functions);
    
    workLoop->removeEventSource(commandGate);
    OSSafeReleaseNULL(commandGate);
    OSSafeReleaseNULL(workLoop);

    PMstop();
    rmi_driver_clear_irq_bits(this);
    
    while (RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject())) {
        func->stop(this);
        func->detach(this);
    }
    
    functions->flushCollection();
    OSSafeReleaseNULL(iter);
    
    super::stop(provider);
}

void RMIBus::free() {
    if (data) {
        rmi_free_function_list(this);
        IOLockFree(data->enabled_mutex);
        IOLockFree(data->irq_mutex);
    }
    
    if (functions)
        OSSafeReleaseNULL(functions);
    super::free();
}

bool RMIBus::willTerminate(IOService *provider, IOOptionBits options) {
    if (transport->isOpen(this)) {
        transport->close(this);
    }
    
    return super::willTerminate(provider, options);
}

int RMIBus::reset()
{
    return transport->reset();
}

int RMIBus::rmi_register_function(rmi_function *fn) {
    RMIFunction * function;

    switch(fn->fd.function_number) {
        case 0x01: /* device control */
            function = OSTypeAlloc(F01);
            break;
        case 0x03: /* PS/2 pass-through */
            function = OSTypeAlloc(F03);
            break;
        case 0x11: /* multifinger pointing */
            function = OSTypeAlloc(F11);
            break;
        case 0x12: /* multifinger pointing */
            function = OSTypeAlloc(F12);
            break;
        case 0x30: /* GPIO and LED controls */
            function = OSTypeAlloc(F30);
            break;
//        case 0x08: /* self test (aka BIST) */
//        case 0x09: /* self test (aka BIST) */
//        case 0x17: /* pointing sticks */
//        case 0x19: /* capacitive buttons */
//        case 0x1A: /* simple capacitive buttons */
//        case 0x21: /* force sensing */
//        case 0x32: /* timer */
        case 0x34: /* device reflash */
//        case 0x36: /* auxiliary ADC */
        case 0x3A:
//        case 0x41: /* active pen pointing */
        case 0x54: /* analog data reporting */
        case 0x55: /* Sensor tuning */
            IOLogInfo("F%X not implemented", fn->fd.function_number);
            return 0;
        default:
            IOLogError("Unknown function: %02X - Continuing to load", fn->fd.function_number);
            return 0;
    }

    if (!function || !function->init()) {
        IOLogError("Could not initialize function: %02X", fn->fd.function_number);
        OSSafeReleaseNULL(function);
        return -ENODEV;
    }

    function->conf = &conf;

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
        IOLogError("Function %02X could not attach", fn->fd.function_number);
        OSSafeReleaseNULL(function);
        return -ENODEV;
    }
    
    functions->setObject(function);
    OSSafeReleaseNULL(function);
    return 0;
}

IOReturn RMIBus::setProperties(OSObject *properties) {
    commandGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &RMIBus::updateConfiguration), OSDynamicCast(OSDictionary, properties));
    return kIOReturnSuccess;
}

void RMIBus::updateConfiguration(OSDictionary* dictionary) {
    if (!dictionary)
        return;

    bool update = false;
    update |= Configuration::loadUInt32Configuration(dictionary, "TrackstickMultiplier", &conf.trackstickMult);
    update |= Configuration::loadUInt32Configuration(dictionary, "TrackstickScrollMultiplierX", &conf.trackstickScrollXMult);
    update |= Configuration::loadUInt32Configuration(dictionary, "TrackstickScrollMultiplierY", &conf.trackstickScrollYMult);
    update |= Configuration::loadUInt32Configuration(dictionary, "TrackstickDeadzone", &conf.trackstickDeadzone);
    update |= Configuration::loadUInt64Configuration(dictionary, "DisableWhileTypingTimeout", &conf.disableWhileTypingTimeout);
    update |= Configuration::loadUInt64Configuration(dictionary, "DisableWhileTrackpointTimeout", &conf.disableWhileTrackpointTimeout);
    update |= Configuration::loadUInt32Configuration(dictionary, "ForceTouchMinPressure", &conf.forceTouchMinPressure);
    update |= Configuration::loadBoolConfiguration(dictionary, "ForceTouchEmulation", &conf.forceTouchEmulation);
    update |= Configuration::loadUInt32Configuration(dictionary, "MinYDiffThumbDetection", &conf.minYDiffGesture);

    if (update) {
        IOLogDebug("Updating Configuration");
        OSDictionary *currentConfig = nullptr;
        OSDictionary *newConfig = nullptr;
        if ((currentConfig = OSDynamicCast(OSDictionary, getProperty("Configuration"))) &&
            (newConfig = OSDictionary::withDictionary(currentConfig)) &&
            (newConfig->merge(dictionary)))
            setProperty("Configuration", newConfig);
        else
            IOLogError("Failed to merge dictionary");
        OSSafeReleaseNULL(newConfig);
    } else {
        IOLogError("Invalid Configuration");
    }
}
