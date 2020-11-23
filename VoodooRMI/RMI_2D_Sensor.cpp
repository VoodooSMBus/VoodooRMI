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

static void fillZone (RMI2DSensorZone *zone, int min_x, int min_y, int max_x, int max_y) {
    zone->x_min = min_x;
    zone->y_min = min_y;
    zone->x_max = max_x;
    zone->y_max = max_y;
}

bool RMI2DSensor::start(IOService *provider)
{
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, max_x, 16);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, max_y, 16);
    // Need to be in 0.01mm units
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, x_mm * 100, 16);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, y_mm * 100, 16);
    setProperty(VOODOO_INPUT_TRANSFORM_KEY, 0ull, 32);
    
    const int palmRejectWidth = max_x * 0.05;
    const int palmRejectHeight = max_y * 0.7;
    const int trackpointRejectHeight = max_y * 0.2;
    
    /*
     * Calculate reject zones.
     * These zones invalidate any fingers within them when typing
     * or using the trackpoint. 0, 0 is top left
     */
    
    // TODO: Make VoodooInput do this instead?
    // TODO: Make configurable and update
    
    // Top left
    fillZone(&rejectZones[0],
             0, 0,
             palmRejectWidth, palmRejectHeight);
    
    // Top right
    fillZone(&rejectZones[1],
             max_x - palmRejectWidth, 0,
             max_x, palmRejectHeight);

    // Top band for trackpoint(and spacebar?)
    fillZone(&rejectZones[2],
             0, 0,
             max_x, trackpointRejectHeight);
    
    setProperty("VoodooInputSupported", kOSBooleanTrue);
    // VoodooPS2 keyboard notifs
    setProperty("RM,deliverNotifications", kOSBooleanTrue);
    
    memset(freeFingerTypes, true, kMT2FingerTypeCount);
    memset(invalidFinger, false, sizeof(invalidFinger));
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
            uint64_t timestamp;
            clock_get_uptime(&timestamp);
            absolutetime_to_nanoseconds(timestamp, &lastTrackpointTS);
            invalidateFingers();
            break;
        // VoodooPS2 Messages
        case kKeyboardKeyPressTime:
            lastKeyboardTS = *((uint64_t*) argument);
            invalidateFingers();
            break;
        case kKeyboardGetTouchStatus: {
            bool *result = (bool *) argument;
            *result = trackpadEnable;
            break;
        }
        case kKeyboardSetTouchStatus:
            trackpadEnable = *((bool *) argument);
            break;
    }
    
    return kIOReturnSuccess;
}

bool RMI2DSensor::shouldDiscardReport(AbsoluteTime timestamp)
{
    return !trackpadEnable;
}

bool RMI2DSensor::checkInZone(VoodooInputTransducer &obj) {
    TouchCoordinates &dimensions = obj.currentCoordinates;
    for (int i = 0; i < 3; i++) {
        RMI2DSensorZone &zone = rejectZones[i];
        if (dimensions.x >= zone.x_min &&
            dimensions.x <= zone.x_max &&
            dimensions.y >= zone.y_min &&
            dimensions.y <= zone.y_max) {
            return true;
        }
    }
    
    return false;
}

#define RMI_2D_MIN_Z 10
#define RMI_2D_MAX_Z 120

void RMI2DSensor::handleReport(RMI2DSensorReport *report)
{
    int realFingerCount = 0;
    
    if (!voodooInputInstance)
        return;
    
    bool discardRegions = (report->timestamp - lastKeyboardTS) < conf->disableWhileTypingTimeout * MILLI_TO_NANO ||
                          (report->timestamp - lastTrackpointTS) < conf->disableWhileTrackpointTimeout * MILLI_TO_NANO;
    
    for (int i = 0; i < report->fingers; i++) {
        rmi_2d_sensor_abs_object obj = report->objs[i];
        
        bool isValid =  obj.type == RMI_2D_OBJECT_FINGER ||
                        obj.type == RMI_2D_OBJECT_STYLUS ||
                        // Allow inaccurate objects to record them as an invalid finger
                        // This can be a random finger or one which was lifted up slightly
                        obj.type == RMI_2D_OBJECT_INACCURATE;
        
        auto& transducer = inputEvent.transducers[i];
        transducer.type = FINGER;
        transducer.isValid = isValid;
        transducer.supportsPressure = true;
        transducer.isTransducerActive = 1;
        transducer.secondaryId = i;
        
        if (isValid) {
            realFingerCount++;
            
            transducer.previousCoordinates = transducer.currentCoordinates;
            transducer.currentCoordinates.width = obj.z / 4.0;
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
            
            int deltaWidth = abs(obj.wx - obj.wy);
            
            // Dissallow large objects
            transducer.isValid = obj.type != RMI_2D_OBJECT_INACCURATE &&
                                 obj.z < RMI_2D_MAX_Z &&
                                 obj.z > RMI_2D_MIN_Z &&
                                 !invalidFinger[i] &&
                                 // Accidental light brushes by the palm generally are tall and skinny
                                 ((obj.z > 50 && transducer.currentCoordinates.y > (max_y / 3)) || deltaWidth < conf->fingerMajorMinorMax) &&
                                 !(discardRegions && checkInZone(transducer));
            
            if (!transducer.isValid)
                invalidFinger[i] = true;
            
            if (clickpadState && conf->forceTouchEmulation && obj.z > conf->forceTouchMinPressure)
                pressureLock = true;
            
            transducer.currentCoordinates.pressure = pressureLock ? 255 : 0;
            transducer.isPhysicalButtonDown = clickpadState && !pressureLock;
            
            IOLogDebug("Finger num: %d (%s) (%d, %d) [Z: %u WX: %u WY: %u FingerType: %d Pressure : %d Button: %d]\n",
                       i, transducer.isValid ? "valid" : "invalid",
                       obj.x, obj.y, obj.z, obj.wx, obj.wy,
                       transducer.fingerType,
                       transducer.currentCoordinates.pressure,
                       transducer.isPhysicalButtonDown);
        } else {
            invalidFinger[i] = false;
        }
    }
    
    if (realFingerCount == 4 && freeFingerTypes[kMT2FingerTypeThumb]) {
        setThumbFingerType(report->fingers, report);
    }
    
    // Sencond loop to get type
    for (int i = 0; i < report->fingers; i++) {
        auto& trans = inputEvent.transducers[i];
        
        if (trans.isValid) {
            if (trans.fingerType == kMT2FingerTypeUndefined) {
                trans.fingerType = getFingerType();
            }
        } else {
            // Free finger
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

// Take the most obvious lowest fingers - otherwise take finger with greatest area
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
        
        if (trans.currentCoordinates.y > minY) {
            lowestFingerIndex = i;
            secondLowest = minY;
            minY = trans.currentCoordinates.y;
        }
        
        if (trans.currentCoordinates.y > secondLowest && trans.currentCoordinates.y < minY) {
            secondLowest = trans.currentCoordinates.y;
        }
        
        if (obj->z > maxArea) {
            maxDiff = (obj->wy - obj->wx);
            maxArea = obj->z;
            greatestFingerIndex = i;
        }
    }
    
    if (minY - secondLowest < conf->minYDiffGesture || greatestFingerIndex == -1) {
        lowestFingerIndex = greatestFingerIndex;
    }
    
    if (lowestFingerIndex == -1) {
        IOLogError("LowestFingerIndex = -1 When there are 4+ fingers");
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
