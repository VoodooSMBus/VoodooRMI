/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_2d_sensor.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include "RMITrackpadFunction.hpp"
#include "RMILogging.h"
#include "RMIMessages.h"
#include <IOKit/IOLib.h>
#include "VoodooInputMultitouch/VoodooInputTransducer.h"

OSDefineMetaClassAndStructors(RMITrackpadFunction, RMIFunction)
#define super RMIFunction

#define RMI_2D_MAX_Z 140
#define RMI_2D_MIN_ZONE_VEL 10
#define RMI_2D_MIN_ZONE_Y_VEL 6
#define RMI_MT2_MAX_PRESSURE 255
#define cfgToPercent(val) ((double) val / 100.0)

static void fillZone (RMI2DSensorZone *zone, int min_x, int min_y, int max_x, int max_y) {
    zone->x_min = min_x;
    zone->y_min = min_y;
    zone->x_max = max_x;
    zone->y_max = max_y;
}

void RMITrackpadFunction::setData(const Rmi2DSensorData &data) {
    this->data = data;
}

const Rmi2DSensorData &RMITrackpadFunction::getData() const {
    return data;
}

bool RMITrackpadFunction::start(IOService *provider)
{
    memset(freeFingerTypes, true, sizeof(freeFingerTypes));
    freeFingerTypes[kMT2FingerTypeUndefined] = false;
 
    for (size_t i = 0; i < MAX_FINGERS; i++) {
        fingerState[i] = RMI_FINGER_LIFTED;
    }
    
    const RmiConfiguration &conf = getConfiguration();
    const int palmRejectWidth = data.maxX * cfgToPercent(conf.palmRejectionWidth);
    const int palmRejectHeight = data.maxY * cfgToPercent(conf.palmRejectionHeight);
    const int trackpointRejectHeight = data.maxY * cfgToPercent(conf.palmRejectionHeightTrackpoint);
    
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
             data.maxX - palmRejectWidth, 0,
             data.maxX, palmRejectHeight);

    // Top band for trackpoint and buttons
    fillZone(&rejectZones[2],
             0, 0,
             data.maxX, trackpointRejectHeight);
    
    // VoodooPS2 keyboard notifs
    setProperty("RM,deliverNotifications", kOSBooleanTrue);
    
    for (int i = 0; i < VOODOO_INPUT_MAX_TRANSDUCERS; i++) {
        auto& transducer = inputEvent.transducers[i];
        transducer.type = FINGER;
        transducer.supportsPressure = true;
        transducer.isValid = 1;
    }
    
    return super::start(provider);
}

IOReturn RMITrackpadFunction::message(UInt32 type, IOService *provider, void *argument)
{
    switch (type)
    {
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

bool RMITrackpadFunction::shouldDiscardReport(AbsoluteTime timestamp)
{
    return !trackpadEnable;
}

// Returns zone that finger is in (or 0 if not in a zone)
size_t RMITrackpadFunction::checkInZone(VoodooInputTransducer &obj) {
    TouchCoordinates &fingerCoords = obj.currentCoordinates;
    for (size_t i = 0; i < 3; i++) {
        RMI2DSensorZone &zone = rejectZones[i];
        if (fingerCoords.x >= zone.x_min &&
            fingerCoords.x <= zone.x_max &&
            fingerCoords.y >= zone.y_min &&
            fingerCoords.y <= zone.y_max) {
            return i+1;
        }
    }
    
    return 0;
}

/**
 * RMI2DSensor::handleReport
 * Takes a report from F11/F12 and converts it for VoodooInput
 * This also does some input rejection.
 * There are three zones on the left, right, and top of the trackpad. If a touch starts in those zones, it is not counted until it exits all zones
 * This also does some sanity checks for very wide or very big touch inputs
 * This checks for force touch on Clickpads only, where the trackpad is able to be pressed down.
 */
void RMITrackpadFunction::handleReport(RMI2DSensorReport *report)
{
    int validFingerCount = 0;
    const RmiConfiguration &conf = getConfiguration();
    
    bool discardRegions = ((report->timestamp - lastKeyboardTS) < (conf.disableWhileTypingTimeout * MILLI_TO_NANO)) ||
                          ((report->timestamp - lastTrackpointTS) < (conf.disableWhileTrackpointTimeout * MILLI_TO_NANO));
    
    size_t maxIdx = report->fingers > MAX_FINGERS ? MAX_FINGERS : report->fingers;
    size_t reportIdx = 0;
    
    nanosecond_ts timestamp;
    absolutetime_to_nanoseconds(report->timestamp, &timestamp);
    
    for (int i = 0; i < maxIdx; i++) {
        rmi_2d_sensor_abs_object obj = report->objs[i];
        
        bool isValidObj = obj.type == RMI_2D_OBJECT_FINGER ||
                          obj.type == RMI_2D_OBJECT_STYLUS; /*||*/
                          // Allow inaccurate objects as they are likely invalid, which we want to track still
                          // This can be a random finger or one which was lifted up slightly
//                          obj.type == RMI_2D_OBJECT_INACCURATE;
        
        auto& transducer = inputEvent.transducers[reportIdx];
        transducer.isTransducerActive = isValidObj;
        transducer.secondaryId = i;
        
        // Finger lifted, make finger valid
        if (!isValidObj) {
            if (fingerState[i] != RMI_FINGER_LIFTED) reportIdx++;
            fingerState[i] = RMI_FINGER_LIFTED;
            transducer.isPhysicalButtonDown = false;
            transducer.currentCoordinates = transducer.previousCoordinates;
            continue;
        }
        
        validFingerCount++;
        reportIdx++;
            
        transducer.previousCoordinates = transducer.currentCoordinates;
        transducer.currentCoordinates.width = obj.z / 2.0;
        transducer.timestamp = report->timestamp;
        
        transducer.currentCoordinates.x = obj.x;
        transducer.currentCoordinates.y = data.maxY - obj.y;
        transducer.isPhysicalButtonDown = clickpadState;
        
        int deltaWidth = abs(obj.wx - obj.wy);
        
        switch (fingerState[i]) {
            case RMI_FINGER_LIFTED:
                fingerState[i] = RMI_FINGER_STARTED_IN_ZONE;
                // Current position is starting position, make sure velocity is zero
                transducer.previousCoordinates = transducer.currentCoordinates;
                fingerContactTime[i] = timestamp;
                fingerContactLoc[i] = transducer.currentCoordinates;
                
                /* fall through */
            case RMI_FINGER_STARTED_IN_ZONE: {
                size_t zone = checkInZone(transducer);
                if (zone == 0 || (timestamp - lastValidTime[i]) < conf.validTimeAfterLift * MILLI_TO_NANO) {
                    fingerState[i] = RMI_FINGER_VALID;
                }
                
                int velocityX = abs((int) transducer.currentCoordinates.x - (int) transducer.previousCoordinates.x);
                int velocityY = abs((int) transducer.currentCoordinates.y - (int) transducer.previousCoordinates.y);

                IOLogDebug("Velocity: %d %d Zone: %ld", velocityX, velocityY, zone);
                if (velocityX > RMI_2D_MIN_ZONE_VEL ||
                    (zone == 3 && velocityY > RMI_2D_MIN_ZONE_Y_VEL)) {
                    fingerState[i] = RMI_FINGER_VALID;
                }
            }
                /* fall through */
            case RMI_FINGER_VALID:
                if (obj.z > RMI_2D_MAX_Z ||
                    deltaWidth > conf.fingerMajorMinorMax) {
                    
                    fingerState[i] = RMI_FINGER_INVALID;
                    freeFingerTypes[transducer.fingerType] = true;
                    transducer.fingerType = (MT2FingerType) 7;
                }
                
                // Force touch emulation only works with clickpads (button underneath trackpad)
                // Lock finger in place and in force touch until lifted
                if (isForceTouch(obj.z)) {
                    fingerState[i] = RMI_FINGER_FORCE_TOUCH;
                }
                
                lastValidTime[i] = timestamp;
                break;
            case RMI_FINGER_FORCE_TOUCH:
                if (!isForceTouch(obj.z)) {
                    fingerState[i] = RMI_FINGER_VALID;
                    transducer.currentCoordinates.pressure = 0;
                    break;
                }
                
                transducer.currentCoordinates = transducer.previousCoordinates;
                transducer.currentCoordinates.pressure = RMI_MT2_MAX_PRESSURE;
                break;
            case RMI_FINGER_INVALID:
                transducer.fingerType = (MT2FingerType) 7;
                continue;
        }
        
        transducer.isTransducerActive = fingerState[i] != RMI_FINGER_LIFTED && fingerState[i] != RMI_FINGER_STARTED_IN_ZONE;
        //beep boop, am typing something, let me see if I accidently tap something if anyhting happens
        // nothing so far, this eems to be going preetty well
        // way better than usual
        IOLogDebug("Finger num: %d (%s) (%d, %d) [Z: %u WX: %u WY: %u FingerType: %d Pressure : %d Button: %d]",
                   i,
                   transducer.isTransducerActive ? "valid" : "invalid",
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
            trans.isTransducerActive = true;
        }
        
        if (trans.isTransducerActive) {
            if (trans.fingerType == kMT2FingerTypeUndefined) {
                trans.fingerType = getFingerType();
            }
        } else {
            // Free finger
            freeFingerTypes[trans.fingerType] = true;
            trans.fingerType = kMT2FingerTypeUndefined;
        }
    }
    
    inputEvent.contact_count = reportIdx;
    inputEvent.timestamp = report->timestamp;
    ;
    if (inputEvent.transducers[0].secondaryId < MAX_FINGERS) {
        if (reportIdx == 1 && fingerState[inputEvent.transducers[0].secondaryId] == RMI_FINGER_LIFTED &&
            timestamp - fingerContactTime[inputEvent.transducers[0].secondaryId] < conf.tapRejectionTimeout * MILLI_TO_NANO &&
            abs((int) inputEvent.transducers[0].currentCoordinates.x - (int) fingerContactLoc[inputEvent.transducers[0].secondaryId].x) < 50 &&
            abs((int) inputEvent.transducers[0].currentCoordinates.y - (int) fingerContactLoc[inputEvent.transducers[0].secondaryId].y) < 50 &&
            report->tapSupported && !report->isTap) {
            IOLogError("Not a tap :(");
            inputEvent.transducers[0].isTransducerActive = true;
            inputEvent.transducers[0].fingerType = (MT2FingerType) 7;
            
            sendVoodooInputPacket(kIOMessageVoodooInputMessage, &inputEvent);
            
            inputEvent.transducers[0].isTransducerActive = false;
            inputEvent.transducers[0].fingerType = kMT2FingerTypeUndefined;
        }
    }
    
    sendVoodooInputPacket(kIOMessageVoodooInputMessage, &inputEvent);
    for (int i = 0; i < VOODOO_INPUT_MAX_TRANSDUCERS; i++) {
        inputEvent.transducers[i].isTransducerActive = false;
    }
}

// Take the most obvious lowest fingers - otherwise take finger with greatest area
void RMITrackpadFunction::setThumbFingerType(size_t maxIdx, RMI2DSensorReport *report)
{
    size_t lowestFingerIndex = -1;
    size_t greatestFingerIndex = -1;
    UInt32 minY = 0, secondLowest = 0;
    UInt32 maxDiff = 0;
    UInt32 maxArea = 0;
    
    const RmiConfiguration &conf = getConfiguration();
    
    for (size_t i = 0; i < maxIdx; i++) {
        auto &trans = inputEvent.transducers[i];
        rmi_2d_sensor_abs_object *obj = &report->objs[i];
        
        if (!trans.isTransducerActive)
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
    
    if (minY - secondLowest < conf.minYDiffGesture || greatestFingerIndex == -1) {
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
MT2FingerType RMITrackpadFunction::getFingerType()
{
    for (MT2FingerType i = kMT2FingerTypeIndexFinger; i < kMT2FingerTypeCount; i = (MT2FingerType)(i + 1)) {
        if (freeFingerTypes[i]) {
            freeFingerTypes[i] = false;
            return i;
        }
    }
    
    return kMT2FingerTypeUndefined;
}

/**
 * RMI2DSensor::invalidateFingers
 * Invalidate fingers which are in zones currently
 * Used when keyboard or trackpoint send events
 */
void RMITrackpadFunction::invalidateFingers() {
    for (size_t i = 0; i < MAX_FINGERS; i++) {
        VoodooInputTransducer &finger = inputEvent.transducers[i];
        
        if (fingerState[i] == RMI_FINGER_LIFTED ||
            fingerState[i] == RMI_FINGER_INVALID)
            continue;
        
        if (checkInZone(finger) > 0) {
            freeFingerTypes[finger.fingerType] = true;
            fingerState[i] = RMI_FINGER_INVALID;
        }
    }
}

bool RMITrackpadFunction::isForceTouch(UInt8 pressure) {
    const RmiConfiguration &conf = getConfiguration();
    switch (conf.forceTouchType) {
        case RMI_FT_DISABLE:
            return false;
        case RMI_FT_CLICK_AND_SIZE:
            return clickpadState && pressure > conf.forceTouchMinPressure;
        case RMI_FT_SIZE:
            return pressure > conf.forceTouchMinPressure;
    }
}
