//
//  F30.cpp
//  VoodooSMBus
//
//  Created by Gwy on 5/6/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#include "F30.hpp"

OSDefineMetaClassAndStructors(F30, RMIFunction)
#define super IOService

bool F30::attach(IOService *provider)
{
    rmiBus = OSDynamicCast(RMIBus, provider);
    if (!rmiBus) {
        IOLogError("F30: No provider.\n");
        return false;
    }
    
    int retval = rmi_f30_initialize();
    
    if (retval < 0)
        return false;
    
    super::attach(provider);
    
    return true;
}

bool F30::start(IOService *provider)
{
    if (!super::start(provider))
        return false;
    // TODO: Either find F03 for trackstick button
    // or just send buttons in attention
    
    int error = rmiBus->blockWrite(fn_descriptor->control_base_addr,
                                   ctrl_regs, ctrl_regs_size);
    
    if (error) {
        IOLogError("%s: Could not write control registers at 0x%x: %d\n",
                   __func__, fn_descriptor->control_base_addr, error);
        return false;;
    }
    
    registerService();
    
    return true;
}

void F30::free()
{
    clearDesc();
    super::free();
}

int F30::rmi_f30_initialize()
{
    u8 *ctrl_reg = ctrl_regs;
    int control_address = fn_descriptor->control_base_addr;
    u8 buf[RMI_F30_QUERY_SIZE];
    int error;
    
    error = rmiBus->readBlock(fn_descriptor->query_base_addr,
                              buf, RMI_F30_QUERY_SIZE);
    if (error) {
        IOLogError("F30: Failed to read query register\n");
        return error;
    }
    
    has_extended_pattern = buf[0] & RMI_F30_EXTENDED_PATTERNS;
    has_mappable_buttons = buf[0] & RMI_F30_HAS_MAPPABLE_BUTTONS;
    has_led = buf[0] & RMI_F30_HAS_LED;
    has_gpio = buf[0] & RMI_F30_HAS_GPIO;
    has_haptic = buf[0] & RMI_F30_HAS_HAPTIC;
    has_gpio_driver_control = buf[0] & RMI_F30_HAS_GPIO_DRV_CTL;
    has_mech_mouse_btns = buf[0] & RMI_F30_HAS_MECH_MOUSE_BTNS;
    gpioled_count = buf[1] & RMI_F30_GPIO_LED_COUNT;
    
    register_count = DIV_ROUND_UP(gpioled_count, 8);
    
    if (has_gpio && has_led)
        rmi_f30_set_ctrl_data(&ctrl[0], &control_address,
                              register_count, &ctrl_reg);
    
    rmi_f30_set_ctrl_data(&ctrl[1], &control_address,
                          sizeof(u8), &ctrl_reg);
    
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
                              sizeof(u8), &ctrl_reg);
    }
    
    if (has_mech_mouse_btns)
        rmi_f30_set_ctrl_data(&ctrl[10], &control_address,
                              sizeof(u8), &ctrl_reg);
    
    ctrl_regs_size = (uint32_t) (ctrl_reg -
        ctrl_regs) ?: RMI_F30_CTRL_REGS_MAX_SIZE;
    
    error = rmi_f30_read_control_parameters();
    if (error) {
        IOLogError("Failed to initialize F30 control params: %d\n",
                   error);
        return error;
    }
    
    if (has_gpio) {
        // TODO: Handle buttons
        // error = rmi_f30_map_gpios(fn, f30);
        if (error)
            return error;
    }
    
    return 0;
}

void F30::rmi_f30_set_ctrl_data(rmi_f30_ctrl_data *ctrl,
                                int *ctrl_addr, int len, u8 **reg)
{
    ctrl->address = *ctrl_addr;
    ctrl->length = len;
    ctrl->regs = *reg;
    *ctrl_addr += len;
    *reg += len;
}

int F30::rmi_f30_read_control_parameters()
{
    int error;
    
    error = rmiBus->readBlock(fn_descriptor->control_base_addr,
                              ctrl_regs, ctrl_regs_size);
    if (error) {
        IOLogError("%s: Could not read control registers at 0x%x: %d\n",
                   __func__, fn_descriptor->control_base_addr, error);
        return error;
    }
    
    return 0;
}
