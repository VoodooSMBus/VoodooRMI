/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F30.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

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
        IOLogError("%s: Could not write control registers at 0x%x: 0x%x\n",
                   __func__, fn_descriptor->control_base_addr, error);
        return false;;
    }
    
    registerService();
//    if (numButtons != 1)
    publishButtons();
    
    return true;
}

void F30::stop(IOService *provider)
{
    unpublishButtons();
    super::stop(provider);
}

void F30::free()
{
    clearDesc();
    super::free();
}

IOReturn F30::message(UInt32 type, IOService *provider, void *argument)
{
//    IOLog("F30 interrupt");
    int button_count = min(gpioled_count, TRACKSTICK_RANGE_END);
    
    switch (type) {
        case kHandleRMIInterrupt:
            int error = rmiBus->readBlock(fn_descriptor->data_base_addr,
                                          data_regs, register_count);
            
            if (error < 0) {
                IOLogError("Could not read F30 data: 0x%x\n", error);
            }
            
            if (has_gpio) {
                int btns = 0;
                for (int i = 0; i < button_count; i++) {
                    if (gpioled_key_map[i] != KEY_RESERVED)
                        btns |= rmi_f30_report_button(i);
                    
                    buttonDevice->updateButtons(btns);
                }
                
            }
            
            break;
    }
    
    return kIOReturnSuccess;
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
        error = rmi_f30_map_gpios();
        if (error)
            return error;
    }
    
    return 0;
}

int F30::rmi_f30_is_valid_button(int button)
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

int F30::rmi_f30_map_gpios()
{
    unsigned int button = BTN_LEFT;
    unsigned int trackstick_button = BTN_LEFT;
    int button_count = min(gpioled_count, TRACKSTICK_RANGE_END);
    setProperty("Button Count", button_count, 32);
    
    gpioled_key_map = reinterpret_cast<uint16_t *>(IOMalloc(button_count * sizeof(gpioled_key_map[0])));
    memset(gpioled_key_map, 0, button_count * sizeof(gpioled_key_map[0]));
    
    for (int i = 0; i < button_count; i++) {
        if (!rmi_f30_is_valid_button(i))
            continue;
        
        if (i >= TRACKSTICK_RANGE_START && i < TRACKSTICK_RANGE_END) {
            gpioled_key_map[i] = trackstick_button++;
        } else {
            IOLog("Found Button %d\n", button);
            gpioled_key_map[i] = button++;
            numButtons++;
            clickpad_index = i;
        }
    }
    
    setProperty("Clickpad", numButtons == 1 ? kOSBooleanTrue : kOSBooleanFalse);
    
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

int F30::rmi_f30_report_button(unsigned int button)
{
    unsigned int reg_num = button >>3;
    unsigned int bit_num = button & 0x07;
    u16 key_code = gpioled_key_map[button];
    bool key_down = !(data_regs[reg_num] & BIT(bit_num));
    
    if (button >= TRACKSTICK_RANGE_START &&
        button <= TRACKSTICK_RANGE_END) {
        if (key_down) IOLog("F30 Trackstick Button");
        return 0;
    } else {
        if (numButtons == 1 && button == clickpad_index) {
            rmiBus->notify(kHandleRMIClickpadSet, key_down);
            return 0;
        } else {
            IOLog("Key %u is %s", key_code, key_down ? "Down": "Up");
            // Key code is one above the shift value as key code 0 is "Reserved" or "not present"
            return ((int) key_down) << (key_code - 1);
        }
    }
}

bool F30::publishButtons() {
    buttonDevice = OSTypeAlloc(ButtonDevice);
    if (!buttonDevice) {
        IOLogError("No memory to allocate TrackpointDevice instance\n");
        goto trackpoint_exit;
    }
    if (!buttonDevice->init(NULL)) {
        IOLogError("Failed to init TrackpointDevice\n");
        goto trackpoint_exit;
    }
    if (!buttonDevice->attach(this)) {
        IOLogError("Failed to attach TrackpointDevice\n");
        goto trackpoint_exit;
    }
    if (!buttonDevice->start(this)) {
        IOLogError("Failed to start TrackpointDevice \n");
        goto trackpoint_exit;
    }
    
    return true;
trackpoint_exit:
    unpublishButtons();
    return false;
}

void F30::unpublishButtons() {
    if (buttonDevice) {
        buttonDevice->stop(this);
        OSSafeReleaseNULL(buttonDevice);
    }
}
