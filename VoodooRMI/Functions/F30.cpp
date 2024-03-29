/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F30.c
 *
 * Copyright (c) 2012-2016 Synaptics Incorporated
 */

#include "RMILogging.h"
#include "RMIConfiguration.hpp"
#include "F30.hpp"
#include "LinuxCompat.h"

OSDefineMetaClassAndStructors(F30, RMIGPIOFunction)
#define super RMIGPIOFunction

void F30::rmi_f30_calc_ctrl_data() {
    UInt8 *ctrl_reg = ctrl_regs;
    int control_address = getCtrlAddr();

    if (has_gpio && has_led)
        rmi_f30_set_ctrl_data(&ctrl[0], &control_address,
                              register_count, &ctrl_reg);

    rmi_f30_set_ctrl_data(&ctrl[1], &control_address,
                          sizeof(UInt8), &ctrl_reg);

    if (has_gpio) {
        rmi_f30_set_ctrl_data(&ctrl[2], &control_address,
                              register_count, &ctrl_reg);

        rmi_f30_set_ctrl_data(&ctrl[3], &control_address,
                              register_count, &ctrl_reg);
    }

    if (has_led) {
        rmi_f30_set_ctrl_data(&ctrl[4], &control_address,
                              register_count, &ctrl_reg);

        rmi_f30_set_ctrl_data(&ctrl[5], &control_address,
                              has_extended_pattern ? 6 : 2,
                              &ctrl_reg);
    }

    if (has_led || has_gpio_driver_control) {
        /* control 6 uses a byte per gpio/led */
        rmi_f30_set_ctrl_data(&ctrl[6], &control_address,
                              gpioled_count, &ctrl_reg);
    }

    if (has_mappable_buttons) {
        /* control 7 uses a byte per gpio/led */
        rmi_f30_set_ctrl_data(&ctrl[7], &control_address,
                              gpioled_count, &ctrl_reg);
    }

    if (has_haptic) {
        rmi_f30_set_ctrl_data(&ctrl[8], &control_address,
                              register_count, &ctrl_reg);

        rmi_f30_set_ctrl_data(&ctrl[9], &control_address,
                              sizeof(UInt8), &ctrl_reg);
    }

    if (has_mech_mouse_btns)
        rmi_f30_set_ctrl_data(&ctrl[10], &control_address,
                              sizeof(UInt8), &ctrl_reg);

    ctrl_regs_size = (uint32_t) (ctrl_reg -
                                 ctrl_regs) ?: RMI_F30_CTRL_REGS_MAX_SIZE;
}

int F30::initialize()
{
    int error;

    query_regs_size = RMI_F30_QUERY_SIZE;
    query_regs = reinterpret_cast<uint8_t *>(IOMalloc(query_regs_size * sizeof(uint8_t)));
    if (!query_regs) {
        IOLogError("%s - Failed to allocate %d query registers", getName(), query_regs_size);
        return -1;
    }
    bzero(query_regs, query_regs_size * sizeof(uint8_t));

    error = readBlock(getQryAddr(),
                      query_regs, RMI_F30_QUERY_SIZE);
    if (error) {
        IOLogError("%s: Failed to read query register: %d", getName(), error);
        return error;
    }
    
    has_extended_pattern = query_regs[0] & RMI_F30_EXTENDED_PATTERNS;
    has_mappable_buttons = query_regs[0] & RMI_F30_HAS_MAPPABLE_BUTTONS;
    has_led = query_regs[0] & RMI_F30_HAS_LED;
    has_gpio = query_regs[0] & RMI_F30_HAS_GPIO;
    has_haptic = query_regs[0] & RMI_F30_HAS_HAPTIC;
    has_gpio_driver_control = query_regs[0] & RMI_F30_HAS_GPIO_DRV_CTL;
    has_mech_mouse_btns = query_regs[0] & RMI_F30_HAS_MECH_MOUSE_BTNS;

    gpioled_count = query_regs[1] & RMI_F30_GPIO_LED_COUNT;
    register_count = DIV_ROUND_UP(gpioled_count, 8);

    OSNumber *value;
    OSDictionary * attribute = OSDictionary::withCapacity(9);
    setPropertyBoolean(attribute, "extended_pattern", has_extended_pattern);
    setPropertyBoolean(attribute, "mappable_buttons", has_mappable_buttons);
    setPropertyBoolean(attribute, "led", has_led);
    setPropertyBoolean(attribute, "gpio", has_gpio);
    setPropertyBoolean(attribute, "haptic", has_haptic);
    setPropertyBoolean(attribute, "gpio_driver_control", has_gpio_driver_control);
    setPropertyBoolean(attribute, "mech_mouse_btns", has_mech_mouse_btns);
    setPropertyNumber(attribute, "gpioled_count", gpioled_count, 8);
    setPropertyNumber(attribute, "register_count", register_count, 8);
    setProperty("Attibute", attribute);
    OSSafeReleaseNULL(attribute);

    // get correct ctrl_regs_size only
    rmi_f30_calc_ctrl_data();

    ctrl_regs = reinterpret_cast<uint8_t *>(IOMalloc(ctrl_regs_size * sizeof(uint8_t)));
    if (!ctrl_regs) {
        IOLogError("%s - Failed to allocate %d control registers", getName(), ctrl_regs_size);
        return -1;
    }
    bzero(ctrl_regs, ctrl_regs_size * sizeof(uint8_t));

    rmi_f30_calc_ctrl_data();

    error = readBlock(getCtrlAddr(), ctrl_regs, ctrl_regs_size);
    if (error) {
        IOLogError("%s - Failed to read control registers: %d", getName(), error);
        return error;
    }
    
    error = mapGpios();
    if (error) {
        IOLogError("%s - Failed to map GPIO: %d", getName(), error);
        return error;
    }
    return 0;
}

bool F30::is_valid_button(int button)
{
    int byte_position = button >> 3;
    int bit_position = button & 0x07;
    
    /*
     * ctrl2 -> dir == 0 -> input mode
     * ctrl3 -> data == 1 -> actual button
     */
    return !(ctrl[2].regs[byte_position] & BIT(bit_position)) &&
            (ctrl[3].regs[byte_position] & BIT(bit_position));
}

void F30::rmi_f30_set_ctrl_data(rmi_f30_ctrl_data *ctrl,
                                int *ctrl_addr, int len, UInt8 **reg)
{
    ctrl->address = *ctrl_addr;
    ctrl->length = len;
    ctrl->regs = *reg;
    *ctrl_addr += len;
    *reg += len;
}
