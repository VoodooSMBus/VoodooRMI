/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Zhen
 * Ported to macOS from linux kernel for Tegra on Android, original source at
 https://android.googlesource.com/kernel/tegra/+/android-7.1.1_r0.79/drivers/input/touchscreen/rmi4/rmi_f17.c
 *
 * Copyright (c) 2012 Synaptics Incorporated
 */

#ifndef F17_hpp
#define F17_hpp

#include <RMITrackpointFunction.hpp>

union f17_device_query {
    struct {
        UInt8 number_of_sticks:3;
    } __attribute__((__packed__));
    UInt8 regs[1];
};

#define F17_MANUFACTURER_SYNAPTICS 0
#define F17_MANUFACTURER_NMB 1
#define F17_MANUFACTURER_ALPS 2

struct f17_stick_query {
    union {
        struct {
            UInt8 manufacturer:4;
            UInt8 resistive:1;
            UInt8 ballistics:1;
            UInt8 reserved1:2;
            UInt8 has_relative:1;
            UInt8 has_absolute:1;
            UInt8 has_gestures:1;
            UInt8 has_dribble:1;
            UInt8 reserved2:4;
        } __attribute__((__packed__));
        UInt8 regs[2];
    } general;
    union {
        struct {
            UInt8 has_single_tap:1;
            UInt8 has_tap_and_hold:1;
            UInt8 has_double_tap:1;
            UInt8 has_early_tap:1;
            UInt8 has_press:1;
        } __attribute__((__packed__));
        UInt8 regs[1];
    } gestures;
};
union f17_device_controls {
    struct {
        UInt8 reporting_mode:3;
        UInt8 dribble:1;
    } __attribute__((__packed__));
    UInt8 regs[1];
};
struct f17_stick_controls {
    union {
        struct {
            UInt8 z_force_threshold;
            UInt8 radial_force_threshold;
        } __attribute__((__packed__));
        UInt8 regs[3];
    } general;
    union {
        struct {
            UInt8 motion_sensitivity:4;
            UInt8 antijitter:1;
        } __attribute__((__packed__));
        UInt8 regs[1];
    } relative;
    union {
        struct {
            UInt8 single_tap:1;
            UInt8 tap_and_hold:1;
            UInt8 double_tap:1;
            UInt8 early_tap:1;
            UInt8 press:1;
        } __attribute__((__packed__));
        UInt8 regs[1];
    } enable;
    UInt8 maximum_tap_time;
    UInt8 minimum_press_time;
    UInt8 maximum_radial_force;
};
union f17_device_commands {
    struct {
        UInt8 rezero:1;
    } __attribute__((__packed__));
    UInt8 regs[1];
};
struct f17_stick_data {
    union {
        struct {
            UInt8 x_force_high:8;
            UInt8 y_force_high:8;
            UInt8 y_force_low:4;
            UInt8 x_force_low:4;
            UInt8 z_force:8;
        } __attribute__((__packed__));
        struct {
            UInt8 regs[4];
            UInt16 address;
        } __attribute__((__packed__));
    } abs;
    union {
        struct {
            SInt8 x_delta:8;
            SInt8 y_delta:8;
        } __attribute__((__packed__));
        struct {
            UInt8 regs[2];
            UInt16 address;
        } __attribute__((__packed__));
    } rel;
    union {
        struct {
            UInt8 single_tap:1;
            UInt8 tap_and_hold:1;
            UInt8 double_tap:1;
            UInt8 early_tap:1;
            UInt8 press:1;
            UInt8 reserved:3;
        } __attribute__((__packed__));
        struct {
            UInt8 regs[1];
            UInt16 address;
        } __attribute__((__packed__));
    } gestures;
};
/* data specific to f17 that needs to be kept around */
struct rmi_f17_stick_data {
    struct f17_stick_query query;
    struct f17_stick_controls controls;
    struct f17_stick_data data;
    UInt16 control_address;
    int index;
};
struct rmi_f17_device_data {
    union f17_device_query query;
    union f17_device_commands commands;
    union f17_device_controls controls;
    struct rmi_f17_stick_data *sticks;
};

class F17 : public RMITrackpointFunction {
    OSDeclareDefaultStructors(F17)
    
public:
    bool attach(IOService *provider) override;
    void free() override;
    void attention() override;
    
    IOReturn config() override;
private:
    rmi_f17_device_data f17;

    int rmi_f17_init_stick(struct rmi_f17_stick_data *stick, UInt16 *next_query_reg, UInt16 *next_data_reg, UInt16 *next_control_reg);
    int rmi_f17_initialize();
    int rmi_f17_process_stick(struct rmi_f17_stick_data *stick);
};

#endif /* F17_hpp */
