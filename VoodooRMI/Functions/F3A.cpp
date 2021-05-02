/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F3A.c
 *
 * Copyright (c) 2012-2020 Synaptics Incorporated
 */

#include "F3A.hpp"

OSDefineMetaClassAndStructors(F3A, RMIFunction)
#define super IOService

bool F3A::attach(IOService *provider)
{
    if (!super::attach(provider))
        return false;
    
    rmiBus = OSDynamicCast(RMIBus, provider);
    if (!rmiBus) {
        IOLogError("F3A - Could not cast RMIBus");
        return false;
    }
    
    u8 query1[RMI_F3A_MAX_REG_SIZE];
    u8 ctrl1[RMI_F3A_MAX_REG_SIZE];
    u8 buf;
    
    IOReturn status = rmiBus->read(fn_descriptor->query_base_addr, &buf);
    if (status != kIOReturnSuccess) {
        IOLogError("F3A - Failed to read general info register!");
        return false;
    }
    
    gpioCount = buf & RMI_F3A_GPIO_COUNT;
    registerCount = DIV_ROUND_UP(gpioCount, 8);
    
    status = rmiBus->readBlock(fn_descriptor->query_base_addr + 1, query1, registerCount);
    if (status != kIOReturnSuccess) {
        IOLogError("F3A - Failed to read query1 registers");
        return false;
    }
    
    status = rmiBus->readBlock(fn_descriptor->control_base_addr + 1, ctrl1, registerCount);
    if (status != kIOReturnSuccess) {
        IOLogError("F3A - Failed to read control1 register");
        return false;
    }
    
    if (!mapGpios(query1, ctrl1)) {
        IOLogError("F3A - Failed to map GPIO");
        return false;
    }
    
    return true;
}

bool F3A::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    voodooTrackpointInstance = rmiBus->getVoodooInput();
    relativeEvent = rmiBus->getRelativePointerEvent();

    registerService();
    return super::start(provider);
}

bool F3A::mapGpios(u8 *query1_regs, u8 *ctrl1_regs)
{
    unsigned int button = BTN_LEFT;
    unsigned int trackpoint_button = BTN_LEFT;
    const gpio_data *gpio = rmiBus->getGPIOData();
    numButtons = min(gpioCount, TRACKPOINT_RANGE_END);
    
    setProperty("Button Count", gpioCount, 32);
    
    gpioled_key_map = reinterpret_cast<uint16_t *>(IOMalloc(numButtons * sizeof(gpioled_key_map[0])));
    if (!gpioled_key_map) {
        IOLogError("Could not create gpioled_key_map!");
        return false;
    }
    
    memset(gpioled_key_map, 0, numButtons * sizeof(gpioled_key_map[0]));
    
    for (int i = 0; i < numButtons; i++) {
        if (!is_valid_button(i, query1_regs, ctrl1_regs))
            continue;
        
        if (gpio->trackpointButtons &&
            (i >= TRACKPOINT_RANGE_START && i < TRACKPOINT_RANGE_END)) {
            IOLogDebug("F3A: Found Trackpoint button %d\n", trackpoint_button);
            gpioled_key_map[i] = trackpoint_button++;
        } else {
            IOLogDebug("F3A: Found Button %d", button);
            gpioled_key_map[i] = button++;
            clickpadIndex = i;
        }
    }
    
    setProperty("Trackpoint Buttons through F3A", trackpoint_button != BTN_LEFT);
    setProperty("Clickpad", numButtons == 1);
    return true;
}


IOReturn F3A::message(UInt32 type, IOService *provider, void *argument)
{
    int error = 0;
    unsigned int mask, trackpointBtns = 0, btns = 0;
    u16 key_code;
    bool key_down;
    
    switch (type) {
        case kHandleRMIAttention:
            if (!voodooTrackpointInstance)
                return kIOReturnIOError;
            
            error = rmiBus->readBlock(fn_descriptor->data_base_addr,
                                      data_regs, registerCount);
            
            if (error < 0) {
                IOLogError("Could not read F3A data: 0x%x", error);
            }
            
            for (int i = 0; i < numButtons; i++) {
                if (gpioled_key_map[i] == KEY_RESERVED)
                    continue;
                
                // Key is down when pulled low
                key_down = !(BIT(i) & data_regs[0]);
                key_code = gpioled_key_map[i];
                mask = key_down << (key_code - 1);
                
                IOLogDebug("Key %u is %s", key_code, key_down ? "Down": "Up");
                
                if (i >= TRACKPOINT_RANGE_START &&
                    i < TRACKPOINT_RANGE_END) {
                    trackpointBtns |= mask;
                } else {
                    btns |= mask;
                }
                
                if (numButtons == 1 && i == clickpadIndex) {
                    if (clickpadState != key_down) {
                         rmiBus->notify(kHandleRMIClickpadSet, key_down);
                         clickpadState = key_down;
                     }
                    continue;
                }
            }
            
            if (numButtons > 1 && voodooTrackpointInstance && *voodooTrackpointInstance) {
                AbsoluteTime timestamp;
                clock_get_uptime(&timestamp);
                
                relativeEvent->dx = relativeEvent->dy = 0;
                relativeEvent->buttons = btns;
                relativeEvent->timestamp = timestamp;
                
                messageClient(kIOMessageVoodooTrackpointRelativePointer, *voodooTrackpointInstance, relativeEvent, sizeof(RelativePointerEvent));
            }
            
            break;
        default:
            return super::message(type, provider, argument);
    }
    
    return kIOReturnSuccess;
}

void F3A::free()
{
    clearDesc();
    if (gpioled_key_map) {
        IOFree(gpioled_key_map, gpioCount * sizeof(gpioled_key_map[0]));
    }
    
    super::free();
}
