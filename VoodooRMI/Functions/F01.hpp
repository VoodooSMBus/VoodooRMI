/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F01.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#ifndef F01_hpp
#define F01_hpp

#include <RMIFunction.hpp>

#define RMI_PRODUCT_ID_LENGTH    10
#define RMI_PRODUCT_INFO_LENGTH   2

#define RMI_DATE_CODE_LENGTH      3

#define PRODUCT_ID_OFFSET 0x10
#define PRODUCT_INFO_OFFSET 0x1E


/* Force a firmware reset of the sensor */
#define RMI_F01_CMD_DEVICE_RESET    1

/* Various F01_RMI_QueryX bits */

#define RMI_F01_QRY1_CUSTOM_MAP        BIT(0)
#define RMI_F01_QRY1_NON_COMPLIANT    BIT(1)
#define RMI_F01_QRY1_HAS_LTS        BIT(2)
#define RMI_F01_QRY1_HAS_SENSOR_ID    BIT(3)
#define RMI_F01_QRY1_HAS_CHARGER_INP    BIT(4)
#define RMI_F01_QRY1_HAS_ADJ_DOZE    BIT(5)
#define RMI_F01_QRY1_HAS_ADJ_DOZE_HOFF    BIT(6)
#define RMI_F01_QRY1_HAS_QUERY42    BIT(7)

#define RMI_F01_QRY5_YEAR_MASK        0x1f
#define RMI_F01_QRY6_MONTH_MASK        0x0f
#define RMI_F01_QRY7_DAY_MASK        0x1f

#define RMI_F01_QRY2_PRODINFO_MASK    0x7f

#define RMI_F01_BASIC_QUERY_LEN        21 /* From Query 00 through 20 */


struct f01_basic_properties {
    UInt8 manufacturer_id;
    bool has_lts;
    bool has_adjustable_doze;
    bool has_adjustable_doze_holdoff;
    char dom[11]; /* YYYY/MM/DD + '\0' */
    char product_id[RMI_PRODUCT_ID_LENGTH + 1];
    UInt16 productinfo;
    UInt32 firmware_id;
    UInt64 package_id;
};

/* F01 device status bits */

/* Most recent device status event */
#define RMI_F01_STATUS_CODE(status)        ((status) & 0x0f)
/* The device has lost its configuration for some reason. */
#define RMI_F01_STATUS_UNCONFIGURED(status)    (!!((status) & 0x80))
/* The device is in bootloader mode */
#define RMI_F01_STATUS_BOOTLOADER(status)    ((status) & 0x40)

/* Control register bits */

/*
 * Sleep mode controls power management on the device and affects all
 * functions of the device.
 */
#define RMI_F01_CTRL0_SLEEP_MODE_MASK    0x03

#define RMI_SLEEP_MODE_NORMAL        0x00
#define RMI_SLEEP_MODE_SENSOR_SLEEP    0x01
#define RMI_SLEEP_MODE_RESERVED0    0x02
#define RMI_SLEEP_MODE_RESERVED1    0x03

/*
 * This bit disables whatever sleep mode may be selected by the sleep_mode
 * field and forces the device to run at full power without sleeping.
 */
#define RMI_F01_CTRL0_NOSLEEP_BIT    BIT(2)

/*
 * When this bit is set, the touch controller employs a noise-filtering
 * algorithm designed for use with a connected battery charger.
 */
#define RMI_F01_CTRL0_CHARGER_BIT    BIT(5)

/*
 * Sets the report rate for the device. The effect of this setting is
 * highly product dependent. Check the spec sheet for your particular
 * touch sensor.
 */
#define RMI_F01_CTRL0_REPORTRATE_BIT    BIT(6)

/*
 * Written by the host as an indicator that the device has been
 * successfully configured.
 */
#define RMI_F01_CTRL0_CONFIGURED_BIT    BIT(7)

/**
 * @ctrl0 - see the bit definitions above.
 * @doze_interval - controls the interval between checks for finger presence
 * when the touch sensor is in doze mode, in units of 10ms.
 * @wakeup_threshold - controls the capacitance threshold at which the touch
 * sensor will decide to wake up from that low power state.
 * @doze_holdoff - controls how long the touch sensor waits after the last
 * finger lifts before entering the doze state, in units of 100ms.
 */
struct f01_device_control {
    UInt8 ctrl0;
    UInt8 doze_interval;
    UInt8 wakeup_threshold;
    UInt8 doze_holdoff;
};

class F01 : public RMIFunction {
    OSDeclareDefaultStructors(F01);
    
public:
    bool attach(IOService *provider) override;
    IOReturn config() override;
    void attention() override;
    
    IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) override;
    
    void setIRQMask(const UInt32 irq, const UInt8 numIrqBits);
    IOReturn readIRQ(UInt32 &irq) const;
    IOReturn setIRQs() const;
    IOReturn clearIRQs() const;
private:
    UInt16 doze_interval_addr;
    UInt16 wakeup_threshold_addr;
    UInt16 doze_holdoff_addr;
    
    bool suspend;
    bool old_nosleep;
    
    f01_basic_properties properties;
    f01_device_control device_control;
    
    UInt8 numIrqRegs;
    UInt32 irqMask;
    
    f01_basic_properties * getProperties();
    void publishProps();
    int rmi_f01_read_properties();
    int rmi_f01_suspend();
    int rmi_f01_resume();
    void rmi_f01_attention();
};

#endif /* F01_hpp */
