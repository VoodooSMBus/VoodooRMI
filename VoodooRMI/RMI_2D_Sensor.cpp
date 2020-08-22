/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_2d_sensor.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "RMI_2D_Sensor.hpp"

OSDefineMetaClassAndStructors(RMI2DSensor, IOService)
#define super IOService

bool RMI2DSensor::init(OSDictionary *dictionary)
{
    if (!super::init())
        return false;

    updateConfiguration(dictionary);
    return true;
}

bool RMI2DSensor::start(IOService *provider)
{
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, max_x, 16);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, max_y, 16);
    // Need to be in 0.01mm units
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, x_mm * 100, 16);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, y_mm * 100, 16);
    setProperty(VOODOO_INPUT_TRANSFORM_KEY, 0ull, 32);
    
    setProperty("VoodooInputSupported", kOSBooleanTrue);
    // VoodooPS2 keyboard notifs
    setProperty("RM,deliverNotifications", kOSBooleanTrue);
    
    memset(freeFingerTypes, true, kMT2FingerTypeCount);
    freeFingerTypes[kMT2FingerTypeUndefined] = false;
    
    memset(invalidFinger, false, 10);
    
    registerService();
    
    return super::start(provider);
}

void RMI2DSensor::free()
{
    if (data_pkt)
        IOFree(data_pkt, pkt_size);
    
    return super::free();
}

bool RMI2DSensor::handleOpen(IOService *forClient, IOOptionBits options, void *arg)
{
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)
        && super::handleOpen(forClient, options, arg)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();
        
        return true;
    }
    
    return false;
}

void RMI2DSensor::handleClose(IOService *forClient, IOOptionBits options)
{
    OSSafeReleaseNULL(voodooInputInstance);
    super::handleClose(forClient, options);
}

void RMI2DSensor::updateConfiguration(OSDictionary *dictionary) {
    if (Configuration::loadUInt64Configuration(dictionary, "DisableWhileTypingTimeout", &disableWhileTypingTimeout)) {
        setProperty("DisableWhileTypingTimeout", disableWhileTypingTimeout, 64);
        disableWhileTypingTimeout *= MilliToNano;
    }
    if (Configuration::loadUInt32Configuration(dictionary, "ForceTouchMinPressure", &forceTouchMinPressure))
        setProperty("ForceTouchMinPressure", forceTouchMinPressure, 32);
    if (Configuration::loadBoolConfiguration(dictionary, "ForceTouchEmulation", &forceTouchEmulation))
        setProperty("ForceTouchEmulation", forceTouchEmulation);
    if (Configuration::loadUInt32Configuration(dictionary, "MinYDiffThumbDetection", &minYDiffGesture))
        setProperty("MinYDiffThumbDetection", minYDiffGesture, 32);
}

IOReturn RMI2DSensor::message(UInt32 type, IOService *provider, void *argument)
{
    switch (type)
    {
        case kHandleRMIInputReport:
            handleReport(reinterpret_cast<RMI2DSensorReport *>(argument));
            break;
        case kHandleRMIClickpadSet:
            clickpadState = !!(argument);
            break;
        case kHandleRMITrackpoint:
            // Re-use keyboard var as it's the same thin
            uint64_t timestamp;
            clock_get_uptime(&timestamp);
            absolutetime_to_nanoseconds(timestamp, &lastKeyboardTS);
            break;
        case kHandleRMIProperties:
            updateConfiguration(reinterpret_cast<OSDictionary *>(argument));
            break;
        // VoodooPS2 Messages
        case kKeyboardKeyPressTime:
            lastKeyboardTS = *((uint64_t*) argument);
            break;
        case kKeyboardGetTouchStatus: {
            bool *result = (bool *) argument;
            *result = touchpadEnable;
            break;
        }
        case kKeyboardSetTouchStatus:
            touchpadEnable = *((bool *) argument);
            break;
    }
    
    return kIOReturnSuccess;
}

bool RMI2DSensor::shouldDiscardReport(AbsoluteTime timestamp)
{
    return  !touchpadEnable ||
            (timestamp - lastKeyboardTS) < disableWhileTypingTimeout;
}

void RMI2DSensor::handleReport(RMI2DSensorReport *report)
{
    int realFingerCount = 0;
    
    if (!voodooInputInstance)
        return;
    
    for (int i = 0; i < report->fingers; i++) {
        rmi_2d_sensor_abs_object obj = report->objs[i];
        
        bool isValid =  obj.type == RMI_2D_OBJECT_FINGER ||
                        obj.type == RMI_2D_OBJECT_STYLUS;
        
        auto& transducer = inputEvent.transducers[i];
        transducer.type = FINGER;
        transducer.isValid = isValid;
        transducer.supportsPressure = true;
        transducer.isTransducerActive = 1;
        transducer.secondaryId = i;
        
        if (isValid) {
            realFingerCount++;
            
            // Dissallow large objects
            transducer.isValid = obj.z < 120 && obj.wx < 7 && obj.wy < 7;
            transducer.previousCoordinates = transducer.currentCoordinates;
            transducer.currentCoordinates.width = obj.z / 1.5;
            transducer.timestamp = report->timestamp;
            
            if (realFingerCount != 1)
                pressureLock = false;
            
            if (!pressureLock) {
                transducer.currentCoordinates.x = obj.x;
                transducer.currentCoordinates.y = max_y - obj.y;
            } else {
                // Lock position for force touch
                transducer.currentCoordinates = transducer.previousCoordinates;
            }
            
            if (clickpadState && forceTouchEmulation && obj.z > forceTouchMinPressure)
                pressureLock = true;
            
            transducer.currentCoordinates.pressure = pressureLock ? 255 : 0;
            transducer.isPhysicalButtonDown = clickpadState && !pressureLock;
            
            IOLogDebug("Finger num: %d (%d, %d) [Z: %u WX: %u WY: %u FingerType: %d Pressure : %d Button: %d]\n",
                       i, obj.x, obj.y, obj.z, obj.wx, obj.wy,
                       transducer.fingerType,
                       transducer.currentCoordinates.pressure,
                       transducer.isPhysicalButtonDown);
        }
    }
    
    if (realFingerCount == 4 && freeFingerTypes[kMT2FingerTypeThumb]) {
        setThumbFingerType(report->fingers, report);
    }
    
    // Sencond loop to get type
    for (int i = 0; i < report->fingers; i++) {
        auto& trans = inputEvent.transducers[i];
        rmi_2d_sensor_abs_object obj = report->objs[i];
        
        if (trans.isValid) {
            // Rudimentry palm detection
            trans.isValid = trans.isValid && (abs(obj.wx - obj.wy) < 3 || trans.fingerType == kMT2FingerTypeThumb);
            
            if (trans.fingerType == kMT2FingerTypeUndefined) {
                trans.fingerType = getFingerType();
            }
        } else {
            // Free finger
            if (trans.fingerType != kMT2FingerTypeUndefined)
                freeFingerTypes[trans.fingerType] = true;
            trans.fingerType = kMT2FingerTypeUndefined;
        }
    }
    
    inputEvent.contact_count = report->fingers;
    inputEvent.timestamp = report->timestamp;
    
    if (!realFingerCount || !clickpadState) {
        pressureLock = false;
    }
    
    messageClient(kIOMessageVoodooInputMessage, voodooInputInstance, &inputEvent, sizeof(VoodooInputEvent));
    memset(report, 0, sizeof(RMI2DSensorReport));
}

void RMI2DSensor::setThumbFingerType(int fingers, RMI2DSensorReport *report)
{
    int lowestFingerIndex = -1;
    int greatestFingerIndex = -1;
    UInt32 minY = 0, secondLowest = 0;
    UInt32 maxDiff = 0;
    UInt32 maxArea = 0;
    
    for (int i = 0; i < fingers; i++) {
        auto &trans = inputEvent.transducers[i];
        rmi_2d_sensor_abs_object *obj = &report->objs[i];
        
        if (!trans.isValid)
            continue;
        
        // Take the most obvious lowest finger - otherwise take finger with greatest area
        if (trans.currentCoordinates.y > minY) {
            lowestFingerIndex = i;
            secondLowest = minY;
            minY = trans.currentCoordinates.y;
        }
        
        if (trans.currentCoordinates.y > secondLowest && trans.currentCoordinates.y < minY) {
            secondLowest = trans.currentCoordinates.y;
        }
        
        if (/*(obj->wy - obj->wx) > maxDiff ||*/ obj->z > maxArea) {
            maxDiff = (obj->wy - obj->wx);
            maxArea = obj->z;
            greatestFingerIndex = i;
        }
    }
    
    if (minY - secondLowest < minYDiffGesture || greatestFingerIndex == -1) {
//        IOLogDebug("Second Lowest: %u Lowest: %u\n", secondLowest, minY);
        
        lowestFingerIndex = greatestFingerIndex;
    }
    
    if (lowestFingerIndex == -1) {
        IOLogError("LowestFingerIndex = -1 When there are 4+ fingers\n");
        return;
    }
    
    auto &trans = inputEvent.transducers[lowestFingerIndex];
    if (trans.fingerType != kMT2FingerTypeUndefined)
        freeFingerTypes[trans.fingerType] = true;
    
    trans.fingerType = kMT2FingerTypeThumb;
    freeFingerTypes[kMT2FingerTypeThumb] = false;
}

MT2FingerType RMI2DSensor::getFingerType()
{
    for (MT2FingerType i = kMT2FingerTypeIndexFinger; i < kMT2FingerTypeCount; i = (MT2FingerType)(i + 1)) {
        if (freeFingerTypes[i]) {
            freeFingerTypes[i] = false;
            return i;
        }
    }
    
    return kMT2FingerTypeUndefined;
}
