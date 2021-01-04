/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_2d_sensor.h
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#ifndef RMI_2D_Sensor_hpp
#define RMI_2D_Sensor_hpp

#include <IOKit/IOService.h>
#include "Utility/LinuxCompat.h"
#include "Utility/Configuration.hpp"
#include "rmi.h"
#include "VoodooInputMultitouch/VoodooInputTransducer.h"
#include "VoodooInputMultitouch/VoodooInputMessages.h"

enum rmi_2d_sensor_object_type {
    RMI_2D_OBJECT_NONE,
    RMI_2D_OBJECT_FINGER,
    RMI_2D_OBJECT_STYLUS,
    RMI_2D_OBJECT_PALM,
    RMI_2D_OBJECT_UNCLASSIFIED,
    RMI_2D_OBJECT_INACCURATE
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
    int fingers;
    AbsoluteTime timestamp;
};

struct RMI2DSensorZone {
    u16 x_min;
    u16 y_min;
    u16 x_max;
    u16 y_max;
};

//struct rmi_2d_sensor {
//    struct rmi_2d_axis_alignment axis_align;
//    int dmax;
//};

/**
 * @axis_align - controls parameters that are useful in system prototyping
 * and bring up.
 * @max_x - The maximum X coordinate that will be reported by this sensor.
 * @max_y - The maximum Y coordinate that will be reported by this sensor.
 * @nbr_fingers - How many fingers can this sensor report?
 * @data_pkt - buffer for data reported by this sensor.
 * @pkt_size - number of bytes in that buffer.
 * @attn_size - Size of the HID attention report (only contains abs data).
 * position when two fingers are on the device.  When this is true, we
 * assume we have one of those sensors and report events appropriately..
 */
class RMI2DSensor : public IOService {
    OSDeclareDefaultStructors(RMI2DSensor)
public:
    u16 min_x{0};
    u16 min_y{0};
    u16 max_x;
    u16 max_y;
    u8 x_mm;
    u8 y_mm;
    
    u8 *data_pkt;
    int pkt_size;
    int attn_size;
    
    u8 report_abs {0};
    u8 report_rel {0};
    
    u8 nbr_fingers;
    
    rmi_configuration *conf;
    IOService **voodooInputInstance {nullptr};

    bool start(IOService *provider) override;
    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    void handleClose(IOService *forClient, IOOptionBits options) override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    void free() override;
    
    bool shouldDiscardReport(AbsoluteTime timestamp);
private:
    int lastFingers;
    
    VoodooInputEvent inputEvent {};
    RMI2DSensorZone rejectZones[3];
    
    bool freeFingerTypes[kMT2FingerTypeCount];
    bool invalidFinger[10];
    bool clickpadState {false};
    bool pressureLock {false};
    bool trackpadEnable {true};
    uint64_t lastKeyboardTS {0}, lastTrackpointTS {0};

    MT2FingerType getFingerType();
    bool checkInZone(VoodooInputTransducer &obj);
    void setThumbFingerType(int fingers, RMI2DSensorReport *report);
    void handleReport(RMI2DSensorReport *report);

    // TODO: move into cpp file
    inline void invalidateFingers() {
        for (int i = 0; i < 5; i++) {
            VoodooInputTransducer &finger = inputEvent.transducers[i];
            
            if (!finger.isValid || invalidFinger[i])
                continue;
            
            if (checkInZone(finger))
                invalidFinger[i] = true;
        }
    }
};

#endif /* RMI_2D_Sensor_hpp */
