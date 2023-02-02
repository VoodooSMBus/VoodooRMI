/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/include/linux/rmi.h
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#ifndef rmi_h
#define rmi_h

#include <IOKit/IOMessage.h>
#include <IOKit/IOLocks.h>
#include "Utility/LinuxCompat.h"

// macOS kernel/math has absolute value in it. It's only for doubles though
#define abs(x) ((x < 0) ? -(x) : (x))

#define DEFAULT_MULT 10
#define MILLI_TO_NANO 1000000

// Message types defined by ApplePS2Keyboard
enum {
    // from keyboard to mouse/trackpad
    kKeyboardSetTouchStatus = iokit_vendor_specific_msg(100),   // set disable/enable trackpad (data is bool*)
    kKeyboardGetTouchStatus = iokit_vendor_specific_msg(101),   // get disable/enable trackpad (data is bool*)
    kKeyboardKeyPressTime = iokit_vendor_specific_msg(110),      // notify of timestamp a non-modifier key was pressed (data is uint64_t*)
    // From SMBus to PS2 Trackpad
    kPS2M_SMBusStart = iokit_vendor_specific_msg(152),          // Reset, disable PS2 comms to not interfere with SMBus comms
};

// RMI Bus message types
enum {
    kHandleRMIAttention = iokit_vendor_specific_msg(2046),
    kHandleRMIClickpadSet = iokit_vendor_specific_msg(2047),
    kHandleRMITrackpoint = iokit_vendor_specific_msg(2050),
    kHandleRMITrackpointButton = iokit_vendor_specific_msg(2051),
    kHandleRMIInputReport = iokit_vendor_specific_msg(2052),
};

/*
 * Set the state of a register
 *    DEFAULT - use the default value set by the firmware config
 *    OFF - explicitly disable the register
 *    ON - explicitly enable the register
 */
enum rmi_reg_state {
    RMI_REG_STATE_DEFAULT = 0,
    RMI_REG_STATE_OFF = 1,
    RMI_REG_STATE_ON = 2
};

struct rmi4_attn_data {
    unsigned long irq_status;
    size_t size;
    void *data;
};

// Force touch types
enum ForceTouchType {
    RMI_FT_DISABLE = 0,
    RMI_FT_CLICK_AND_SIZE = 1,
    RMI_FT_SIZE = 2,
};

struct RmiConfiguration {
    /* F03 */
    uint32_t trackpointMult {DEFAULT_MULT};
    uint32_t trackpointScrollXMult {DEFAULT_MULT};
    uint32_t trackpointScrollYMult {DEFAULT_MULT};
    uint32_t trackpointDeadzone {1};
    /* RMI2DSensor */
    uint32_t forceTouchMinPressure {80};
    uint32_t minYDiffGesture {200};
    uint32_t fingerMajorMinorMax {10};
    // Time units are in milliseconds
    uint64_t disableWhileTypingTimeout {2000};
    uint64_t disableWhileTrackpointTimeout {2000};
    // Percentage out of 100
    uint8_t palmRejectionWidth {15};
    uint8_t palmRejectionHeight {80};
    uint8_t palmRejectionHeightTrackpoint {20};
    ForceTouchType forceTouchType {RMI_FT_CLICK_AND_SIZE};
};

// Data for F30 and F3A
struct RmiGpioData {
    bool clickpad {false};
    bool trackpointButtons {true};
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
    u16 rezero_wait;
    bool topbuttonpad;
    bool kernel_tracking;
    int dmax;
    int dribble;
    int palm_detect;
};

#endif /* rmi_h */
