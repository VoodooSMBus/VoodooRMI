/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Sensor Controller for macOS
 *
 * Copyright (c) 2021 Avery Black
 */

#include "RMIBus.hpp"
#include "RMIFunction.hpp"

OSDefineMetaClassAndStructors(RMIBus, IOService)
OSDefineMetaClassAndStructors(RMIFunction, IOService)
OSDefineMetaClassAndStructors(RMITransport, IOService)
#define super IOService

bool RMIBus::init(OSDictionary *dictionary) {
    if (!super::init(dictionary))
        return false;
    
    functions = OSSet::withCapacity(5);

    updateConfiguration(OSDynamicCast(OSDictionary, getProperty("Configuration")));
    return true;
}

bool RMIBus::start(IOService *provider) {
    int retval;
    OSDictionary *config;
    
#if DEBUG
    IOLogInfo("RMI Bus (DEBUG) Starting up!");
#else
    IOLogInfo("RMI Bus (RELEASE) Starting up!");
#endif // DEBUG
    
    if (!super::start(provider)) {
        return false;
    }
    
    transport = OSDynamicCast(RMITransport, provider);
    if (transport == nullptr) {
        IOLogError("Could not get transport instance");
        return false;
    }
    
    workLoop = IOWorkLoop::workLoop();
    commandGate = IOCommandGate::commandGate(this);
    
    if (workLoop == nullptr || commandGate == nullptr ||
        workLoop->addEventSource(commandGate) != kIOReturnSuccess) {
        IOLogError("%s Failed to add commandGate", getName());
        return false;
    }
    
    // GPIO data from VoodooPS2
    if (OSObject *object = transport->getProperty("GPIO Data")) {
        OSDictionary *dict = OSDynamicCast(OSDictionary, object);
        getGPIOData(dict);
    }

    retval = rmi_init_functions(this, data);
    if (retval)
        goto err;

    retval = rmi_enable_sensor(this);
    if (retval) {
        goto err;
    }
    
    if (!transport->open(this))
        return false;

    config = transport->createConfig();
    if (config != nullptr) {
        updateConfiguration(config);
        OSSafeReleaseNULL(config);
    }

    registerService();
    return true;
err:
    IOLogError("Could not start");
    return false;
}

void RMIBus::handleHostNotify()
{
    unsigned long mask, irqStatus;
    
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
    if (error < 0){
        IOLogError("Unable to read IRQ");
        return;
    }
    
    data->irq_status = irqStatus;

    IOLockLock(data->irq_mutex);
    mask = data->irq_status & data->fn_irq_bits;
    IOLockUnlock(data->irq_mutex);
    
    OSIterator* iter = OSCollectionIterator::withCollection(functions);
    if (!iter) {
        IOLogDebug("RMIBus::handleHostNotify: No Iter");
        return;
    }
    
    while(RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject())) {
        if (func->getIRQ() & mask) {
            messageClient(kHandleRMIAttention, func);
        }
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

void RMIBus::handleReset()
{
    if (!data || !data->f01_container) {
        IOLogDebug("Device not ready for reset, ignoring...");
        return;
    }
    
    int error = readBlock(data->f01_container->fd.control_base_addr + 1,
                          reinterpret_cast<u8 *>(&data->current_irq_mask),
                          data->num_of_irq_regs);
    
    if (error < 0) {
        IOLogError("Failed to read current IRQ mask during reset!");
        return;
    }
    
    messageClients(kHandleRMIConfig);
}

IOReturn RMIBus::message(UInt32 type, IOService *provider, void *argument) {
    IOReturn err;
    
    switch (type) {
        case kIOMessageVoodooI2CHostNotify:
        case kIOMessageVoodooSMBusHostNotify:
            handleHostNotify();
            break;
        case kIOMessageVoodooI2CLegacyHostNotify:
            handleHostNotifyLegacy();
            break;
        case kIOMessageRMI4ResetHandler:
            handleReset();
            break;
        case kIOMessageRMI4Sleep:
            IOLogDebug("Sleep");
            messageClients(kHandleRMISleep);
            rmi_driver_clear_irq_bits(this);
            break;
        case kIOMessageRMI4Resume:
            IOLogDebug("Wakeup");
            err = rmi_driver_set_irq_bits(this);
            if (err < 0) {
                IOLogError("Could not wakeup device");
                return kIOReturnError;
            }
            messageClients(kHandleRMIResume);
            break;
        default:
            return super::message(type, provider);
    }
    
    return kIOReturnSuccess;
}

void RMIBus::notify(UInt32 type, void *argument)
{
    if (type == kHandleRMIClickpadSet ||
        type == kHandleRMITrackpoint) {
        
        messageClient(type, trackpadFunction, argument);
    } else if (type == kHandleRMITrackpointButton) {
        
        messageClient(type, trackpointFunction, argument);
    }
}

void RMIBus::stop(IOService *provider) {
    OSIterator *iter = OSCollectionIterator::withCollection(functions);
    
    workLoop->removeEventSource(commandGate);
    OSSafeReleaseNULL(commandGate);
    OSSafeReleaseNULL(workLoop);

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

IOReturn RMIBus::setProperties(OSObject *properties) {
    commandGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &RMIBus::updateConfiguration), OSDynamicCast(OSDictionary, properties));
    return kIOReturnSuccess;
}

void RMIBus::updateConfiguration(OSDictionary* dictionary) {
    if (!dictionary)
        return;

    bool update = false;
    update |= Configuration::loadUInt32Configuration(dictionary, "TrackpointMultiplier", &conf.trackpointMult);
    update |= Configuration::loadUInt32Configuration(dictionary, "TrackpointScrollMultiplierX", &conf.trackpointScrollXMult);
    update |= Configuration::loadUInt32Configuration(dictionary, "TrackpointScrollMultiplierY", &conf.trackpointScrollYMult);
    update |= Configuration::loadUInt32Configuration(dictionary, "TrackpointDeadzone", &conf.trackpointDeadzone);
    update |= Configuration::loadUInt64Configuration(dictionary, "DisableWhileTypingTimeout", &conf.disableWhileTypingTimeout);
    update |= Configuration::loadUInt64Configuration(dictionary, "DisableWhileTrackpointTimeout", &conf.disableWhileTrackpointTimeout);
    update |= Configuration::loadUInt32Configuration(dictionary, "ForceTouchMinPressure", &conf.forceTouchMinPressure);
    update |= Configuration::loadUInt32Configuration(dictionary, "ForceTouchType", reinterpret_cast<UInt32 *>(&conf.forceTouchType));
    update |= Configuration::loadUInt32Configuration(dictionary, "MinYDiffThumbDetection", &conf.minYDiffGesture);
    update |= Configuration::loadUInt32Configuration(dictionary, "FingerMajorMinorDiffMax", &conf.fingerMajorMinorMax);
    update |= Configuration::loadUInt8Configuration(dictionary, "PalmRejectionWidth", &conf.palmRejectionWidth);
    update |= Configuration::loadUInt8Configuration(dictionary, "PalmRejectionHeight", &conf.palmRejectionHeight);
    update |= Configuration::loadUInt8Configuration(dictionary, "PalmRejectionTrackpointHeight", &conf.palmRejectionHeightTrackpoint);

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

void RMIBus::getGPIOData(OSDictionary *dict) {
    if (!dict)
        return;
    
    Configuration::loadBoolConfiguration(dict, "Clickpad", &gpio.clickpad);
    Configuration::loadBoolConfiguration(dict, "TrackstickButtons", &gpio.trackpointButtons);
    
    setProperty("GPIO Data", dict);
    
    IOLogInfo("Recieved GPIO Data");
}
