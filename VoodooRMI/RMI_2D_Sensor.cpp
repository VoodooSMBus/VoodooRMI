/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_2d_sensor.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "RMI_2D_Sensor.hpp"

OSDefineMetaClassAndStructors(RMI2DSensor, IOService)
#define super IOService

#define RMI_2D_MAX_Z 140
#define RMI_2D_MIN_ZONE_VEL 10
#define RMI_MT2_MAX_PRESSURE 255
#define cfgToPercent(val) ((double) val / 100.0)

static void fillZone (RMI2DSensorZone *zone, int min_x, int min_y, int max_x, int max_y) {
    zone->x_min = min_x;
    zone->y_min = min_y;
    zone->x_max = max_x;
    zone->y_max = max_y;
}

bool RMI2DSensor::start(IOService *provider)
{
    memset(fingerState, RMI_FINGER_LIFTED, sizeof(fingerState));
    memset(freeFingerTypes, true, sizeof(freeFingerTypes));
    freeFingerTypes[kMT2FingerTypeUndefined] = false;
    
    const int palmRejectWidth = max_x * cfgToPercent(conf->palmRejectionWidth);
    const int palmRejectHeight = max_y * cfgToPercent(conf->palmRejectionHeight);
    const int trackpointRejectHeight = max_y * cfgToPercent(conf->palmRejectionHeightTrackpoint);
    
    /*
     * Calculate reject zones.
     * These zones invalidate any fingers within them when typing
     * or using the trackpoint. 0, 0 is top left
     */
    
    // Top left
    fillZone(&rejectZones[0],
             0, 0,
             palmRejectWidth, palmRejectHeight);
    
    // Top right
    fillZone(&rejectZones[1],
             max_x - palmRejectWidth, 0,
             max_x, palmRejectHeight);

    // Top band for trackpoint and buttons
    fillZone(&rejectZones[2],
             0, 0,
             max_x, trackpointRejectHeight);
    
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, max_x, 16);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, max_y, 16);
    // Need to be in 0.01mm units
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, x_mm * 100, 16);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, y_mm * 100, 16);
    setProperty(VOODOO_INPUT_TRANSFORM_KEY, 0ull, 32);
    
    // VoodooPS2 keyboard notifs
    setProperty("RM,deliverNotifications", kOSBooleanTrue);
    setProperty("VoodooInputSupported", kOSBooleanTrue);
    registerService();
    
    for (int i = 0; i < VOODOO_INPUT_MAX_TRANSDUCERS; i++) {
        auto& transducer = inputEvent.transducers[i];
        transducer.type = FINGER;
        transducer.supportsPressure = true;
        transducer.isTransducerActive = 1;
    }
    
    return super::start(provider);
}

bool RMI2DSensor::handleOpen(IOService *forClient, IOOptionBits options, void *arg)
{
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)
        && super::handleOpen(forClient, options, arg)) {
        *voodooInputInstance = forClient;
        (*voodooInputInstance)->retain();
        
        return true;
    }
    
    return false;
}

void RMI2DSensor::handleClose(IOService *forClient, IOOptionBits options)
{
    OSSafeReleaseNULL(*voodooInputInstance);
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
    TouchCoordinates &fingerCoords = obj.currentCoordinates;
    for (int i = 0; i < 3; i++) {
        RMI2DSensorZone &zone = rejectZones[i];
        if (fingerCoords.x >= zone.x_min &&
            fingerCoords.x <= zone.x_max &&
            fingerCoords.y >= zone.y_min &&
            fingerCoords.y <= zone.y_max) {
            return true;
        }
    }
    
    return false;
}

/**
 * RMI2DSensor::handleReport
 * Takes a report from F11/F12 and converts it for VoodooInput
 * This also does some input rejection.
 * There are three zones on the left, right, and top of the trackpad. If a touch starts in those zones, it is not counted until it exits all zones
 * This also does some sanity checks for very wide or very big touch inputs
 * This checks for force touch on Clickpads only, where the trackpad is able to be pressed down.
 */
void RMI2DSensor::handleReport(RMI2DSensorReport *report)
{
    int validFingerCount = 0;
    
    if (!voodooInputInstance || !*voodooInputInstance)
        return;
    
    bool discardRegions = ((report->timestamp - lastKeyboardTS) < (conf->disableWhileTypingTimeout * MILLI_TO_NANO)) ||
                          ((report->timestamp - lastTrackpointTS) < (conf->disableWhileTrackpointTimeout * MILLI_TO_NANO));
    
    size_t maxIdx = report->fingers > MAX_FINGERS ? MAX_FINGERS : report->fingers;
    for (int i = 0; i < maxIdx; i++) {
        rmi_2d_sensor_abs_object obj = report->objs[i];
        
        bool isValidObj = obj.type == RMI_2D_OBJECT_FINGER ||
                          obj.type == RMI_2D_OBJECT_STYLUS ||
                          // Allow inaccurate objects as they are likely invalid, which we want to track still
                          // This can be a random finger or one which was lifted up slightly
                          obj.type == RMI_2D_OBJECT_INACCURATE;
        
        auto& transducer = inputEvent.transducers[i];
        transducer.isValid = isValidObj;
        transducer.secondaryId = i;
        
        // Finger lifted, make finger valid
        if (!isValidObj) {
            fingerState[i] = RMI_FINGER_LIFTED;
            transducer.currentCoordinates.pressure = 0;
            continue;
        }
        
        validFingerCount++;
            
        transducer.previousCoordinates = transducer.currentCoordinates;
        transducer.currentCoordinates.width = obj.z / 2.0;
        transducer.timestamp = report->timestamp;
        
        transducer.currentCoordinates.x = obj.x;
        transducer.currentCoordinates.y = max_y - obj.y;
        
        int deltaWidth = abs(obj.wx - obj.wy);
        
        switch (fingerState[i]) {
            case RMI_FINGER_LIFTED:
                fingerState[i] = RMI_FINGER_STARTED_IN_ZONE;
                // Current position is starting position, make sure velocity is zero
                transducer.previousCoordinates = transducer.currentCoordinates;
                
                /* fall through */
            case RMI_FINGER_STARTED_IN_ZONE: {
                if (!checkInZone(transducer)) {
                    fingerState[i] = RMI_FINGER_VALID;
                }
                
                int velocityX = abs((int) transducer.currentCoordinates.x - (int) transducer.previousCoordinates.x);
                if (velocityX > RMI_2D_MIN_ZONE_VEL) {
                    fingerState[i] = RMI_FINGER_VALID;
                }
            }
                /* fall through */
            case RMI_FINGER_VALID:
                if (obj.z > RMI_2D_MAX_Z ||
                    deltaWidth > conf->fingerMajorMinorMax) {
                    
                    fingerState[i] = RMI_FINGER_INVALID;
                }
                
                transducer.isPhysicalButtonDown = clickpadState;
                
                // Force touch emulation only works with clickpads (button underneath trackpad)
                // Lock finger in place and in force touch until lifted
                if (clickpadState && conf->forceTouchEmulation && obj.z > conf->forceTouchMinPressure) {
                    fingerState[i] = RMI_FINGER_FORCE_TOUCH;
                }
                
                break;
            case RMI_FINGER_FORCE_TOUCH:
                if (!clickpadState && obj.z < conf->forceTouchMinPressure) {
                    fingerState[i] = RMI_FINGER_VALID;
                    transducer.currentCoordinates.pressure = 0;
                    break;
                }
                
                transducer.isPhysicalButtonDown = false;
                transducer.currentCoordinates = transducer.previousCoordinates;
                transducer.currentCoordinates.pressure = RMI_MT2_MAX_PRESSURE;
                break;
            case RMI_FINGER_INVALID:
                transducer.isValid = false;
                continue;
        }
        
        transducer.isValid = fingerState[i] == RMI_FINGER_VALID || fingerState[i] == RMI_FINGER_FORCE_TOUCH;
        
        IOLogDebug("Finger num: %d (%s) (%d, %d) [Z: %u WX: %u WY: %u FingerType: %d Pressure : %d Button: %d]",
                   i,
                   transducer.isValid ? "valid" : "invalid",
                   obj.x, obj.y, obj.z, obj.wx, obj.wy,
                   transducer.fingerType,
                   transducer.currentCoordinates.pressure,
                   transducer.isPhysicalButtonDown);
    }
    
    if (validFingerCount >= 4 && freeFingerTypes[kMT2FingerTypeThumb]) {
        setThumbFingerType(maxIdx, report);
    }
    
    bool isGesture = !discardRegions && validFingerCount > 2;
    
    // Second loop to get finger type and allow gestures
    for (size_t i = 0; i < maxIdx; i++) {
        auto& trans = inputEvent.transducers[i];
        
        if (isGesture &&
            fingerState[i] == RMI_FINGER_STARTED_IN_ZONE) {
            trans.isValid = true;
        }
        
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
    
    messageClient(kIOMessageVoodooInputMessage, *voodooInputInstance, &inputEvent, sizeof(VoodooInputEvent));
}

// Take the most obvious lowest fingers - otherwise take finger with greatest area
void RMI2DSensor::setThumbFingerType(size_t maxIdx, RMI2DSensorReport *report)
{
    size_t lowestFingerIndex = -1;
    size_t greatestFingerIndex = -1;
    UInt32 minY = 0, secondLowest = 0;
    UInt32 maxDiff = 0;
    UInt32 maxArea = 0;
    
    for (size_t i = 0; i < maxIdx; i++) {
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

// Assign the first free finger (other than the thumb)
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

void RMI2DSensor::invalidateFingers() {
    for (size_t i = 0; i < MAX_FINGERS; i++) {
        VoodooInputTransducer &finger = inputEvent.transducers[i];
        
        if (fingerState[i] == RMI_FINGER_INVALID)
            continue;
        
        if (checkInZone(finger))
            fingerState[i] = RMI_FINGER_INVALID;
    }
}
