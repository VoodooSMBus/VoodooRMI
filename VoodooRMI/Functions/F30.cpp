/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F30.c
 *
 * Copyright (c) 2012-2016 Synaptics Incorporated
 */

#include "F30.hpp"

OSDefineMetaClassAndStructors(F30, RMIFunction)
#define super IOService

bool F30::attach(IOService *provider)
{
    rmiBus = OSDynamicCast(RMIBus, provider);
    if (!rmiBus) {
        IOLogError("F30: No provider.");
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
    
    int ret = rmi_f30_config();
    if (ret < 0) return false;
    
    voodooTrackpointInstance = rmiBus->getVoodooInput();
    relativeEvent = rmiBus->getRelativePointerEvent();

    registerService();
    return true;
}

void F30::stop(IOService *provider)
{
    super::stop(provider);
}

void F30::free()
{
    clearDesc();
    super::free();
}

IOReturn F30::message(UInt32 type, IOService *provider, void *argument)
{
    int error;
    switch (type) {
        case kHandleRMIAttention:
            error = rmiBus->readBlock(fn_descriptor->data_base_addr,
                                          data_regs, register_count);
            
            if (error < 0) {
                IOLogError("Could not read F30 data: 0x%x", error);
            }
            
            if (!has_gpio)
                return kIOReturnSuccess;
            
            rmi_f30_report_button();
            break;
        case kHandleRMIConfig:
            return rmi_f30_config();
    }
    
    return kIOReturnSuccess;
}

int F30::rmi_f30_config()
{
    int error = rmiBus->blockWrite(fn_descriptor->control_base_addr,
                                   ctrl_regs, ctrl_regs_size);
    
    if (error) {
        IOLogError("%s: Could not write control registers at 0x%x: 0x%x",
                   __func__, fn_descriptor->control_base_addr, error);
    }
    return error;
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
        IOLogError("F30: Failed to read query register");
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
        IOLogError("Failed to initialize F30 control params: %d",
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
    unsigned int trackpoint_button = BTN_LEFT;
    int buttonArrLen = min(gpioled_count, TRACKPOINT_RANGE_END);
    const gpio_data *gpio = rmiBus->getGPIOData();
    setProperty("Button Count", buttonArrLen, 32);
    
    gpioled_key_map = reinterpret_cast<uint16_t *>(IOMalloc(buttonArrLen * sizeof(gpioled_key_map[0])));
    memset(gpioled_key_map, 0, buttonArrLen * sizeof(gpioled_key_map[0]));
    
    for (int i = 0; i < buttonArrLen; i++) {
        if (!rmi_f30_is_valid_button(i))
            continue;
        
        if (gpio->trackpointButtons &&
            (i >= TRACKPOINT_RANGE_START && i < TRACKPOINT_RANGE_END)) {
            IOLogDebug("F30: Found Trackpoint button %d\n", trackpoint_button);
            gpioled_key_map[i] = trackpoint_button++;
        } else {
            IOLogDebug("F30: Found Button %d", button);
            gpioled_key_map[i] = button++;
            numButtons++;
            clickpad_index = i;
        }
    }
    
    // Trackpoint buttons either come through F03/PS2 passtrough OR they come through F30 interrupts
    // Generally I've found it more common for them to come through PS2
    hasTrackpointButtons = trackpoint_button != BTN_LEFT;
    setProperty("Trackpoint Buttons through F30", hasTrackpointButtons);
    setProperty("Clickpad", numButtons == 1);
    
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
        IOLogError("%s: Could not read control registers at 0x%x: %d",
                   __func__, fn_descriptor->control_base_addr, error);
        return error;
    }
    
    return 0;
}

void F30::rmi_f30_report_button()
{
    int buttonArrLen = min(gpioled_count, TRACKPOINT_RANGE_END);
    unsigned int mask, btns = 0;
    unsigned int reg_num, bit_num;
    u16 key_code;
    bool key_down;
    
    for (int i = 0; i < buttonArrLen; i++) {
        if (gpioled_key_map[i] == KEY_RESERVED)
            continue;
        
        reg_num = i >> 3;
        bit_num = i & 0x07;
        key_code = gpioled_key_map[i];
        key_down = !(data_regs[reg_num] & BIT(bit_num));
        // Key code is one above the value we need to bitwise shift left, as key code 0 is "Reserved" or "not present"
        mask = key_down << (key_code - 1);
        
        IOLogDebug("Button %d Key %u is %s", i, key_code, key_down ? "Down": "Up");
        
        if (numButtons == 1 && i == clickpad_index) {
            if (clickpadState != key_down) {
                 rmiBus->notify(kHandleRMIClickpadSet, key_down);
                 clickpadState = key_down;
             }
            continue;
        }
        
        btns |= mask;
    }
    
    if (numButtons > 1 && voodooTrackpointInstance && *voodooTrackpointInstance) {
        AbsoluteTime timestamp;
        clock_get_uptime(&timestamp);
        
        relativeEvent->dx = relativeEvent->dy = 0;
        relativeEvent->buttons = btns;
        relativeEvent->timestamp = timestamp;
        
        messageClient(kIOMessageVoodooTrackpointRelativePointer, *voodooTrackpointInstance, relativeEvent, sizeof(RelativePointerEvent));
    }
}
