/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Sensor Controller for macOS
 *
 * Copyright (c) 2021 Avery Black
 */

#include "RMIBus.hpp"
#include "RMIFunction.hpp"
#include "RMITrackpadFunction.hpp"
#include "RMIConfiguration.hpp"
#include "RMILogging.h"
#include "RMIMessages.h"
#include "F01.hpp"
#include "VoodooInputMessages.h"

OSDefineMetaClassAndStructors(RMIBus, IOService)
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
    
    // Scan page descripton table to find all functionality
    // This is where trackpad/trackpoint/button capability is found
    retval = rmiScanPdt();
    if (retval) {
        goto err;
    }

    // Configure all functions then enable IRQs
    retval = rmiEnableSensor();
    if (retval) {
        goto err;
    }
    
    // Ready for interrupts
    setProperty(RMIBusIdentifier, kOSBooleanTrue);
    if (!transport->open(this)) {
        IOLogError("Could not open transport");
        goto err;
    }

    // Check for any ACPI configuration
    config = transport->createConfig();
    if (config != nullptr) {
        updateConfiguration(config);
        OSSafeReleaseNULL(config);
    }

    publishVoodooInputProperties();
    registerService();
    return true;
err:
    OSIterator *iter = OSCollectionIterator::withCollection(functions);
    
    while (RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject())) {
        func->stop(this);
        func->detach(this);
    }
    
    functions->flushCollection();
    OSSafeReleaseNULL(iter);
    IOLogError("Could not start");
    return false;
}

void RMIBus::handleHostNotify() {
    UInt32 irqStatus;
    
    if (controlFunction == nullptr) {
        IOLogError("Interrupt - No F01");
        return;
    }
    
    IOReturn error = controlFunction->readIRQ(irqStatus);
    
    if (error != kIOReturnSuccess){
        IOLogError("Unable to read IRQ");
        return;
    }
    
    OSIterator* iter = OSCollectionIterator::withCollection(functions);
    if (!iter) {
        IOLogDebug("RMIBus::handleHostNotify: No Iter");
        return;
    }
    
    while(RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject())) {
        if (func->hasAttnSig(irqStatus)) {
            func->attention();
        }
    }
    
    OSSafeReleaseNULL(iter);
}

void RMIBus::handleHostNotifyLegacy() {
     if (controlFunction == nullptr) {
         IOLogError("Interrupt - No F01");
     }

     OSIterator* iter = OSCollectionIterator::withCollection(functions);
     while(RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject()))
         func->attention();
     OSSafeReleaseNULL(iter);
}

IOReturn RMIBus::message(UInt32 type, IOService *provider, void *argument) {
    switch (type) {
        case kIOMessageVoodooI2CHostNotify:
        case kIOMessageVoodooSMBusHostNotify:
            handleHostNotify();
            break;
        case kIOMessageVoodooI2CLegacyHostNotify:
            handleHostNotifyLegacy();
            break;
        case kIOMessageRMI4ResetHandler:
            rmiEnableSensor();
            break;
        case kIOMessageRMI4Sleep:
            IOLogInfo("Sleep");
            return controlFunction->clearIRQs();
        case kIOMessageRMI4Resume:
            IOLogInfo("Wakeup");
            return rmiEnableSensor();
        default:
            return super::message(type, provider);
    }
    
    return kIOReturnSuccess;
}

void RMIBus::notify(UInt32 type, void *argument) {
    if (type == kHandleRMIClickpadSet ||
        type == kHandleRMITrackpoint) {
        
        messageClient(type, trackpadFunction, argument);
    } else if (type == kHandleRMITrackpointButton) {
        
        messageClient(type, trackpointFunction, argument);
    }
}

void RMIBus::stop(IOService *provider) {
    OSIterator *iter = OSCollectionIterator::withCollection(functions);
    
    while (RMIFunction *func = OSDynamicCast(RMIFunction, iter->getNextObject())) {
        func->stop(this);
        func->detach(this);
    }
    
    functions->flushCollection();
    OSSafeReleaseNULL(iter);
    
    super::stop(provider);
}

void RMIBus::free() {
    workLoop->removeEventSource(commandGate);
    OSSafeReleaseNULL(commandGate);
    OSSafeReleaseNULL(workLoop);
    OSSafeReleaseNULL(functions);
    super::free();
}

bool RMIBus::willTerminate(IOService *provider, IOOptionBits options) {
    if (transport->isOpen(this)) {
        transport->close(this);
    }
    
    return super::willTerminate(provider, options);
}

IOReturn RMIBus::setProperties(OSObject *properties) {
    commandGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &RMIBus::updateConfiguration), OSDynamicCast(OSDictionary, properties));
    publishVoodooInputProperties();
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
    update |= Configuration::loadUInt8Configuration(dictionary, "PalmRejectionMaxObjWidth", &conf.palmRejectionMaxObjWidth);
    update |= Configuration::loadUInt8Configuration(dictionary, "PalmRejectionMaxObjHeight", &conf.palmRejectionMaxObjHeight);
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

// Make sure all functions are configured, then enable IRQs so we get data
IOReturn RMIBus::rmiEnableSensor() {
    RMIFunction *func;
    OSIterator *iter;
    
    if (controlFunction == nullptr) {
        IOLogDebug("Device not ready for reset, ignoring...");
        return kIOReturnSuccess;
    }
    
    iter = getClientIterator();
    while ((func = OSDynamicCast(RMIFunction, iter->getNextObject()))) {
        if (func->config() != kIOReturnSuccess) {
            IOLogError("Could not start function %s", func->getName());
        }
    }
    
    OSSafeReleaseNULL(iter);
    return controlFunction->setIRQs();
}

// MARK: VoodooInput

void RMIBus::publishVoodooInputProperties() {
    OSNumber *value;
    
    if (trackpadFunction == nullptr) {
        // VoodooInput requires trackpad properties to exist to attach
        // Don't bother if there is no trackpad
        return;
    }
    
    const Rmi2DSensorData &trackpadData = trackpadFunction->getData();
    
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, trackpadData.maxX, 16);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, trackpadData.maxY, 16);
    // Need to be in 0.01mm units
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, trackpadData.sizeX * 100, 32);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, trackpadData.sizeY * 100, 32);
    setProperty(VOODOO_INPUT_TRANSFORM_KEY, 0ull, 32);
    
    if (trackpointFunction != nullptr) {
        OSDictionary *trackpoint = OSDictionary::withCapacity(5);
        if (trackpoint == nullptr)
            return;
        
        setPropertyNumber(trackpoint, VOODOO_TRACKPOINT_DEADZONE, conf.trackpointDeadzone, 32);
        setPropertyNumber(trackpoint, VOODOO_TRACKPOINT_BTN_CNT, 3, 32);
        setPropertyNumber(trackpoint, VOODOO_TRACKPOINT_MOUSE_MULT_X, conf.trackpointMult, 32);
        setPropertyNumber(trackpoint, VOODOO_TRACKPOINT_MOUSE_MULT_Y, conf.trackpointMult, 32);
        setPropertyNumber(trackpoint, VOODOO_TRACKPOINT_MOUSE_DIV_X, DEFAULT_MULT, 32);
        setPropertyNumber(trackpoint, VOODOO_TRACKPOINT_MOUSE_DIV_Y, DEFAULT_MULT, 32);
        setPropertyNumber(trackpoint, VOODOO_TRACKPOINT_SCROLL_MULT_X, conf.trackpointScrollXMult, 32);
        setPropertyNumber(trackpoint, VOODOO_TRACKPOINT_SCROLL_MULT_Y, conf.trackpointScrollYMult, 32);
        setPropertyNumber(trackpoint, VOODOO_TRACKPOINT_SCROLL_DIV_X, DEFAULT_MULT, 32);
        setPropertyNumber(trackpoint, VOODOO_TRACKPOINT_SCROLL_DIV_Y, DEFAULT_MULT, 32);
        setProperty(VOODOO_TRACKPOINT_KEY, trackpoint);
        OSSafeReleaseNULL(trackpoint);
    }
    
    setProperty("VoodooInputSupported", kOSBooleanTrue);
    messageClient(kIOMessageVoodooTrackpointUpdatePropertiesNotification, voodooInputInstance);
}

bool RMIBus::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    if (voodooInputInstance != nullptr) {
        // Already opened
        return false;
    }
    
    if (forClient != nullptr && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();
        return true;
    }
    
    return false;
}

void RMIBus::handleClose(IOService *forClient, IOOptionBits options) {
    if (forClient == voodooInputInstance && forClient != nullptr) {
        OSSafeReleaseNULL(voodooInputInstance);
    }
}

bool RMIBus::handleIsOpen(const IOService *forClient) const {
    if (forClient == nullptr) {
        return voodooInputInstance != nullptr;
    } else {
        return voodooInputInstance == forClient;
    }
}
