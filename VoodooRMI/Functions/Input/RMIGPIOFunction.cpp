/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F30.c
 *
 * Copyright (c) 2012-2016 Synaptics Incorporated
 */

#include "RMIGPIOFunction.hpp"

OSDefineMetaClassAndStructors(RMIGPIOFunction, RMIFunction)
#define super RMIFunction

bool RMIGPIOFunction::attach(IOService *provider)
{
    int error;

    error = initialize();
    if (error)
        return false;

    super::attach(provider);

    return true;
}

int RMIGPIOFunction::readControlParameters()
{
    int error;

    error = bus->readBlock(desc.control_base_addr,
                              ctrl_regs, ctrl_regs_size);
    if (error) {
        IOLogError("%s: Could not read control registers at 0x%x: %d",
                   getName(), desc.control_base_addr, error);
        return error;
    }

    return 0;
}

int RMIGPIOFunction::config()
{
    /* Write Control Register values back to device */
    int error = bus->blockWrite(desc.control_base_addr,
                                   ctrl_regs, ctrl_regs_size);

    if (error) {
        IOLogError("%s: Could not write control registers at 0x%x: %d",
                   getName(), desc.control_base_addr, error);
    }
    return error;
}

int RMIGPIOFunction::mapGpios()
{
    unsigned int button = BTN_LEFT;
    unsigned int trackpoint_button = BTN_LEFT;
    int buttonArrLen = min(gpioled_count, TRACKPOINT_RANGE_END);
    const gpio_data *gpio = bus->getGPIOData();
    setProperty("Button Count", buttonArrLen, 32);

    for (int i = 0; i < buttonArrLen; i++) {
        if (!is_valid_button(i))
            continue;

        if (gpio->trackpointButtons &&
            (i >= TRACKPOINT_RANGE_START && i < TRACKPOINT_RANGE_END)) {
            IOLogDebug("%s: Found Trackpoint button %d at %d\n", getName(), trackpoint_button, i);
            gpioled_key_map[i] = trackpoint_button++;
        } else {
            IOLogDebug("%s: Found Button %d at %d", getName(), button, i);
            gpioled_key_map[i] = button++;
            numButtons++;
            clickpadIndex = i;
        }
    }

    // Trackpoint buttons either come through F03/PS2 passtrough OR they come through GPIO interrupts
    // Generally I've found it more common for them to come through PS2
    hasTrackpointButtons = trackpoint_button != BTN_LEFT;
    setProperty("Trackpoint Buttons through GPIO", hasTrackpointButtons);
    setProperty("Clickpad", numButtons == 1);

    return 0;
}

IOReturn RMIGPIOFunction::message(UInt32 type, IOService *provider, void *argument)
{
    int error;
    switch (type) {
        case kHandleRMIAttention:
            error = bus->readBlock(desc.data_base_addr,
                                          data_regs, register_count);

            if (error < 0) {
                IOLogError("Could not read %s data: %d", getName(), error);
            }

            if (has_gpio)
                reportButton();
            break;
        case kHandleRMIConfig:
            return config();
        default:
            return super::message(type, provider, argument);
    }

    return kIOReturnSuccess;
}

void RMIGPIOFunction::reportButton()
{
    unsigned int mask, trackpointBtns = 0, btns = 0;
    unsigned int reg_num, bit_num;
    u16 key_code;
    bool key_down;

    for (int i = 0; i < min(gpioled_count, TRACKPOINT_RANGE_END); i++) {
        if (gpioled_key_map[i] == KEY_RESERVED)
            continue;

        // Key is down when pulled low
        reg_num = i >> 3;
        bit_num = i & 0x07;
        key_down = !(data_regs[reg_num] & BIT(bit_num));
        // simplified in F3A:
        // key_down = !(BIT(i) & data_regs[0]);
        key_code = gpioled_key_map[i];
        // Key code is one above the value we need to bitwise shift left, as key code 0 is "Reserved" or "not present"
        mask = key_down << (key_code - 1);

        IOLogDebug("Button %d Key %u is %s", i, key_code, key_down ? "Down": "Up");

        if (numButtons == 1 && i == clickpadIndex) {
            if (clickpadState != key_down) {
                 bus->notify(kHandleRMIClickpadSet, (void *)key_down);
                 clickpadState = key_down;
             }
            continue;
        }

        if (i >= TRACKPOINT_RANGE_START &&
            i < TRACKPOINT_RANGE_END) {
            trackpointBtns |= mask;
        } else {
            btns |= mask;
        }
    }

    IOService *voodooInputInstance = bus->getVoodooInput();
    if (numButtons > 1 && voodooInputInstance) {
        AbsoluteTime timestamp;
        clock_get_uptime(&timestamp);

        relativeEvent.dx = relativeEvent.dy = 0;
        relativeEvent.buttons = btns;
        relativeEvent.timestamp = timestamp;

        messageClient(kIOMessageVoodooTrackpointRelativePointer, voodooInputInstance, &relativeEvent, sizeof(RelativePointerEvent));
    }

    if (hasTrackpointButtons) {
        bus->notify(kHandleRMITrackpointButton, reinterpret_cast<void *>(trackpointBtns));
    }
}
