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

#include <IOKit/IOService.h>

// macOS kernel/math has absolute value in it. It's only for doubles though
#define abs(x) ((x < 0) ? -(x) : (x))

#define DEFAULT_MULT 10
#define MILLI_TO_NANO 1000000

#define TRACKPOINT_RANGE_START      3
#define TRACKPOINT_RANGE_END        6

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
    kHandleRMISleep = iokit_vendor_specific_msg(2048),
    kHandleRMIResume = iokit_vendor_specific_msg(2049),
    kHandleRMITrackpoint = iokit_vendor_specific_msg(2050),
//    kHandleRMITrackpointButton = iokit_vendor_specific_msg(2051),
    kHandleRMIInputReport = iokit_vendor_specific_msg(2052),
    kHandleRMIConfig = iokit_vendor_specific_msg(2053),
};

/*
 * The interrupt source count in the function descriptor can represent up to
 * 6 interrupt sources in the normal manner.
 */
#define RMI_FN_MAX_IRQS    6

/**
 * struct rmi_function_descriptor - RMI function base addresses
 *
 * @ query_base_addr: The RMI Query base address
 * @ command_base_addr: The RMI Command base address
 * @ control_base_addr: The RMI Control base address
 * @ data_base_addr: The RMI Data base address
 * @ interrupt_source_count: The number of irqs this RMI function needs
 * @ function_number: The RMI function number
 *
 * This struct is used when iterating the Page Description Table. The addresses
 * are 16-bit values to include the current page address.
 *
 */
struct rmi_function_descriptor {
    u16 query_base_addr;
    u16 command_base_addr;
    u16 control_base_addr;
    u16 data_base_addr;
    u8 interrupt_source_count;
    u8 function_number;
    u8 function_version;
};

/**
 * struct rmi_function - represents the implementation of an RMI4
 * function for a particular device (basically, a driver for that RMI4 function)
 *
 * @fd: The function descriptor of the RMI function
 * @rmi_dev: Pointer to the RMI device associated with this function container
 * @dev: The device associated with this particular function.
 *
 * @num_of_irqs: The number of irqs needed by this function
 * @irq_pos: The position in the irq bitfield this function holds
 * @irq_mask: For convenience, can be used to mask IRQ bits off during ATTN
 * interrupt handling.
 * @irqs: assigned virq numbers (up to num_of_irqs)
 *
 * @node: entry in device's list of functions
 */
struct rmi_function {
    int size;
    struct rmi_function_descriptor fd;
    
    unsigned int num_of_irqs;
    int irq[RMI_FN_MAX_IRQS];
    unsigned int irq_pos;
    unsigned long irq_mask[];
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

struct __kfifo {
    unsigned int    in;
    unsigned int    out;
    unsigned int    mask;
    unsigned int    esize;
    void        *data;
};

struct rmi_configuration {
    /* F03 */
    uint32_t trackpointMult {DEFAULT_MULT};
    uint32_t trackpointScrollXMult {DEFAULT_MULT};
    uint32_t trackpointScrollYMult {DEFAULT_MULT};
    uint32_t trackpointDeadzone {1};
    /* RMI2DSensor */
    bool forceTouchEmulation {true};
    uint32_t forceTouchMinPressure {80};
    uint32_t minYDiffGesture {200};
    uint32_t fingerMajorMinorMax {3};
    // Time units are in milliseconds
    uint64_t disableWhileTypingTimeout {2000};
    uint64_t disableWhileTrackpointTimeout {2000};
};

// Data for F30 and F3A
struct gpio_data {
    bool clickpad {false};
    bool trackpointButtons {true}; // Does not affect F03
};

/*
 *  Wrapper class for functions
 */
class RMIFunction : public IOService {
    OSDeclareDefaultStructors(RMIFunction)
    
public:
    inline void setFunctionDesc(rmi_function_descriptor *desc) {
        this->fn_descriptor = desc;
    }
    
    inline void setMask(unsigned long irqMask) {
        irq_mask = irqMask;
    }
    
    inline void setIrqPos(unsigned int irqPos) {
        irqPos = irqPos;
    }
    
    inline unsigned long getIRQ() {
        return irq_mask;
    }
    
    inline unsigned int getIRQPos() {
        return irqPos;
    }
    
    inline void clearDesc() {
        if(this->fn_descriptor)
            IOFree(this->fn_descriptor, sizeof(rmi_function_descriptor));
        return;
    }

    rmi_configuration *conf;

private:
    unsigned long irq_mask;
    unsigned int irqPos;
protected:
    rmi_function_descriptor *fn_descriptor;
};

/**
 * struct rmi_2d_axis_alignment - target axis alignment
 * @swap_axes: set to TRUE if desired to swap x- and y-axis
 * @flip_x: set to TRUE if desired to flip direction on x-axis
 * @flip_y: set to TRUE if desired to flip direction on y-axis
 * @clip_x_low - reported X coordinates below this setting will be clipped to
 *               the specified value
 * @clip_x_high - reported X coordinates above this setting will be clipped to
 *               the specified value
 * @clip_y_low - reported Y coordinates below this setting will be clipped to
 *               the specified value
 * @clip_y_high - reported Y coordinates above this setting will be clipped to
 *               the specified value
 * @offset_x - this value will be added to all reported X coordinates
 * @offset_y - this value will be added to all reported Y coordinates
 * @rel_report_enabled - if set to true, the relative reporting will be
 *               automatically enabled for this sensor.
 */
struct rmi_2d_axis_alignment {
    bool swap_axes;
    bool flip_x;
    bool flip_y;
    u16 clip_x_low;
    u16 clip_y_low;
    u16 clip_x_high;
    u16 clip_y_high;
    u16 offset_x;
    u16 offset_y;
    u8 delta_x_threshold;
    u8 delta_y_threshold;
};

/** This is used to override any hints an F11 2D sensor might have provided
 * as to what type of sensor it is.
 *
 * @rmi_f11_sensor_default - do not override, determine from F11_2D_QUERY14 if
 * available.
 * @rmi_f11_sensor_touchscreen - treat the sensor as a touchscreen (direct
 * pointing).
 * @rmi_f11_sensor_trackpad - thread the sensor as a trackpad (indirect
 * pointing).
 */
enum rmi_sensor_type {
    rmi_sensor_default = 0,
    rmi_sensor_touchscreen,
    rmi_sensor_trackpad
};

#define RMI_F11_DISABLE_ABS_REPORT      BIT(0)

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
    struct rmi_2d_axis_alignment axis_align;
    enum rmi_sensor_type sensor_type;
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

struct rmi_driver_data {
    rmi_function *f01_container;
    rmi_function *f34_container;
    bool bootloader_mode;
    
    int num_of_irq_regs;
    int irq_count;
    unsigned long irq_status;
    unsigned long fn_irq_bits;
    unsigned long current_irq_mask;
    unsigned long new_irq_mask;
    IOLock *irq_mutex;
    
    struct irq_domain *irqdomain;
    
    u8 pdt_props;
    
    u8 num_rx_electrodes;
    u8 num_tx_electrodes;
    
    bool enabled;
    IOLock *enabled_mutex;
};

#endif /* rmi_h */
