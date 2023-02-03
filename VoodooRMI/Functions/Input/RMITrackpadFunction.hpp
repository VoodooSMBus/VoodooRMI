/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_2d_sensor.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#ifndef RMITrackpadFunction_hpp
#define RMITrackpadFunction_hpp

#include "RMIFunction.hpp"
#include <IOKit/IOService.h>
#include <LinuxCompat.h>
#include <RMIConfiguration.hpp>
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

/**
 * struct rmi_2d_sensor_data - overrides defaults for a 2D sensor.
 * @axis_align - provides axis alignment overrides (see above).
 * @sensor_type - Forces the driver to treat the sensor as an indirect
 * pointing device (trackpad) rather than a direct pointing device
 * (touchscreen).  This is useful when F11_2D_QUERY14 register is not
 * available.
 * @disable_report_mask - Force data to not be reported even if it is supported
 * by the firware.
 * @topbuttonpad - Used with the "5 buttons trackpads" found on the Lenovo 40
 * series
 * @kernel_tracking - most moderns RMI f11 firmwares implement Multifinger
 * Type B protocol. However, there are some corner cases where the user
 * triggers some jumps by tapping with two fingers on the trackpad.
 * Use this setting and dmax to filter out these jumps.
 * Also, when using an old sensor using MF Type A behavior, set to true to
 * report an actual MT protocol B.
 * @dmax - the maximum distance (in sensor units) the kernel tracking allows two
 * distincts fingers to be considered the same.
 */
struct rmi_2d_sensor_platform_data {
    int x_mm;
    int y_mm;
    int disable_report_mask;
    UInt16 rezero_wait;
    bool topbuttonpad;
    bool kernel_tracking;
    int dmax;
    int dribble;
    int palm_detect;
};

struct rmi_2d_sensor_abs_object {
    enum rmi_2d_sensor_object_type type;
    UInt16 x;
    UInt16 y;
    UInt8 z;
    UInt8 wx;
    UInt8 wy;
};

struct RMI2DSensorReport {
    rmi_2d_sensor_abs_object objs[10];
    size_t fingers;
    AbsoluteTime timestamp;
};

struct RMI2DSensorZone {
    UInt16 x_min;
    UInt16 y_min;
    UInt16 x_max;
    UInt16 y_max;
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
    UInt16 min_x{0};
    UInt16 min_y{0};
    UInt16 max_x;
    UInt16 max_y;
    UInt8 x_mm;
    UInt8 y_mm;
    
    UInt8 report_abs {0};
    UInt8 report_rel {0};
    
    UInt8 nbr_fingers;
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
    bool isForceTouch(UInt8 pressure);
};

#endif /* RMITrackpadFunction_hpp */
