//
//  RMITrackpadFunction.hpp
//  VoodooRMI
//
//  Created by Sheika Slate on 9/7/21.
//  Copyright © 2021 1Revenger1. All rights reserved.
//

#ifndef RMITrackpadFunction_hpp
#define RMITrackpadFunction_hpp

#include "RMIFunction.hpp"
#include <IOKit/IOService.h>
#include <LinuxCompat.h>
#include <Configuration.hpp>
#include "VoodooInputMultitouch/VoodooInputMessages.h"

#define MAX_FINGERS 10

enum rmi_2d_sensor_object_type {
    RMI_2D_OBJECT_NONE,
    RMI_2D_OBJECT_FINGER,
    RMI_2D_OBJECT_STYLUS,
    RMI_2D_OBJECT_PALM,
    RMI_2D_OBJECT_UNCLASSIFIED,
    RMI_2D_OBJECT_INACCURATE
};

enum finger_state {
    RMI_FINGER_INVALID = 0,     // Invalid finger
    RMI_FINGER_LIFTED,          // Finger is not on trackpad currently (starting state)
    RMI_FINGER_STARTED_IN_ZONE, // Finger put down in palm rejection zone
    RMI_FINGER_VALID,           // Valid finger to be sent to macOS
    RMI_FINGER_FORCE_TOUCH,     // Force touch
};

struct rmi_2d_sensor_abs_object {
    enum rmi_2d_sensor_object_type type;
    u16 x;
    u16 y;
    u8 z;
    u8 wx;
    u8 wy;
};

struct RMI2DSensorReport {
    rmi_2d_sensor_abs_object objs[10];
    size_t fingers;
    AbsoluteTime timestamp;
};

struct RMI2DSensorZone {
    u16 x_min;
    u16 y_min;
    u16 x_max;
    u16 y_max;
};

/**
 * @axis_align - controls parameters that are useful in system prototyping
 * and bring up.
 * @max_x - The maximum X coordinate that will be reported by this sensor.
 * @max_y - The maximum Y coordinate that will be reported by this sensor.
 * @nbr_fingers - How many fingers can this sensor report?
 * position when two fingers are on the device.  When this is true, we
 * assume we have one of those sensors and report events appropriately..
 */
class RMITrackpadFunction : public RMIFunction {
    OSDeclareDefaultStructors(RMITrackpadFunction)
public:
    bool start(IOService *provider) override;
    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    void handleClose(IOService *forClient, IOOptionBits options) override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    
protected:
    u16 min_x{0};
    u16 min_y{0};
    u16 max_x;
    u16 max_y;
    u8 x_mm;
    u8 y_mm;
    
    u8 report_abs {0};
    u8 report_rel {0};
    
    u8 nbr_fingers;
    IOService *voodooInputInstance {nullptr};
    
    void handleReport(RMI2DSensorReport *report);
    bool shouldDiscardReport(AbsoluteTime timestamp);
private:
    VoodooInputEvent inputEvent {};
    RMI2DSensorZone rejectZones[3];
    
    bool freeFingerTypes[kMT2FingerTypeCount];
    finger_state fingerState[MAX_FINGERS];
    bool clickpadState {false};
    bool trackpadEnable {true};
    uint64_t lastKeyboardTS {0}, lastTrackpointTS {0};

    MT2FingerType getFingerType();
    size_t checkInZone(VoodooInputTransducer &obj);
    void setThumbFingerType(size_t fingers, RMI2DSensorReport *report);
    void invalidateFingers();
};

#endif /* RMITrackpadFunction_hpp */
