/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Zhen
 * Ported to macOS from linux kernel for Tegra on Android, original source at
 https://android.googlesource.com/kernel/tegra/+/android-7.1.1_r0.79/drivers/input/touchscreen/rmi4/rmi_f17.c
 *
 * Copyright (c) 2012 Synaptics Incorporated
 */

#ifndef F17_hpp
#define F17_hpp

#include <RMIBus.hpp>

union f17_device_query {
    struct {
        u8 number_of_sticks:3;
    } __attribute__((__packed__));
    u8 regs[1];
};

#define F17_MANUFACTURER_SYNAPTICS 0
#define F17_MANUFACTURER_NMB 1
#define F17_MANUFACTURER_ALPS 2

struct f17_stick_query {
    union {
        struct {
            u8 manufacturer:4;
            u8 resistive:1;
            u8 ballistics:1;
            u8 reserved1:2;
            u8 has_relative:1;
            u8 has_absolute:1;
            u8 has_gestures:1;
            u8 has_dribble:1;
            u8 reserved2:4;
        } __attribute__((__packed__));
        u8 regs[2];
    } general;
    union {
        struct {
            u8 has_single_tap:1;
            u8 has_tap_and_hold:1;
            u8 has_double_tap:1;
            u8 has_early_tap:1;
            u8 has_press:1;
        } __attribute__((__packed__));
        u8 regs[1];
    } gestures;
};
union f17_device_controls {
    struct {
        u8 reporting_mode:3;
        u8 dribble:1;
    } __attribute__((__packed__));
    u8 regs[1];
};
struct f17_stick_controls {
    union {
        struct {
            u8 z_force_threshold;
            u8 radial_force_threshold;
        } __attribute__((__packed__));
        u8 regs[3];
    } general;
    union {
        struct {
            u8 motion_sensitivity:4;
            u8 antijitter:1;
        } __attribute__((__packed__));
        u8 regs[1];
    } relative;
    union {
        struct {
            u8 single_tap:1;
            u8 tap_and_hold:1;
            u8 double_tap:1;
            u8 early_tap:1;
            u8 press:1;
        } __attribute__((__packed__));
        u8 regs[1];
    } enable;
    u8 maximum_tap_time;
    u8 minimum_press_time;
    u8 maximum_radial_force;
};
union f17_device_commands {
    struct {
        u8 rezero:1;
    } __attribute__((__packed__));
    u8 regs[1];
};
struct f17_stick_data {
    union {
        struct {
            u8 x_force_high:8;
            u8 y_force_high:8;
            u8 y_force_low:4;
            u8 x_force_low:4;
            u8 z_force:8;
        } __attribute__((__packed__));
        struct {
            u8 regs[4];
            u16 address;
        } __attribute__((__packed__));
    } abs;
    union {
        struct {
            s8 x_delta:8;
            s8 y_delta:8;
        } __attribute__((__packed__));
        struct {
            u8 regs[2];
            u16 address;
        } __attribute__((__packed__));
    } rel;
    union {
        struct {
            u8 single_tap:1;
            u8 tap_and_hold:1;
            u8 double_tap:1;
            u8 early_tap:1;
            u8 press:1;
            u8 reserved:3;
        } __attribute__((__packed__));
        struct {
            u8 regs[1];
            u16 address;
        } __attribute__((__packed__));
    } gestures;
};
/* data specific to f17 that needs to be kept around */
struct rmi_f17_stick_data {
    struct f17_stick_query query;
    struct f17_stick_controls controls;
    struct f17_stick_data data;
    u16 control_address;
    int index;
//    char input_phys[NAME_BUFFER_SIZE];
//    struct input_dev *input;
//    char mouse_phys[NAME_BUFFER_SIZE];
//    struct input_dev *mouse;
};
struct rmi_f17_device_data {
//    u16 control_address;
    union f17_device_query query;
    union f17_device_commands commands;
    union f17_device_controls controls;
    struct rmi_f17_stick_data *sticks;
};

#define RMI_F17_QUERY_SIZE             1

/* Defs for Device Query */
#define RMI_F17_NUM_OF_STICKS          0x07

/* Defs for Stick Query 0 - General */
#define RMI_F17_MANUFACTURER           0x0f
#define RMI_F17_RESISTIVE              BIT(4)
#define RMI_F17_BALLISTICS             BIT(5)

/* Defs for Stick Query 1 - General*/
#define RMI_F17_HAS_RELATIVE           0x01
#define RMI_F17_HAS_ABSOLUTE           BIT(1)
#define RMI_F17_HAS_GESTURES           BIT(2)
#define RMI_F17_HAS_DRIBBLE            BIT(3)

/* Defs for Stick Query 2 - Gestures  */
#define RMI_F17_HAS_SINGLE_TAP         0x01
#define RMI_F17_HAS_TAP_AND_HOLD       BIT(1)
#define RMI_F17_HAS_DOUBLE_TAP         BIT(2)
#define RMI_F17_HAS_EARLY_TAP          BIT(3)
#define RMI_F17_HAS_PRESS              BIT(4)

/* Defs for Device Control */
#define RMI_F17_CTRL_REPORTING_MODE    0x07
#define RMI_F17_CTRL_DRIBBLE           BIT(3)

/* Defs for Stick Control */

class F17 : public RMIFunction {
    OSDeclareDefaultStructors(F17)
    
public:
    bool init(OSDictionary *dictionary) override;
    bool attach(IOService *provider) override;
    bool start(IOService *provider) override;
//    void stop(IOService *providerr) override;
    void free() override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;

private:
    RMIBus *rmiBus;
    IOService **voodooTrackpointInstance{nullptr};
    RelativePointerEvent relativeEvent {};

//    u8 numSticks {0};

    rmi_f17_device_data *f17 {nullptr};

    int rmi_f17_init_stick(struct rmi_f17_stick_data *stick, u16 *next_query_reg, u16 *next_data_reg, u16 *next_control_reg);
    int rmi_f17_initialize();
    int rmi_f17_config();
    //    int f17_read_control_parameters();
    int rmi_f17_process_stick(struct rmi_f17_stick_data *stick);
};

#endif /* F17_hpp */
