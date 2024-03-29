/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F30.c
 *
 * Copyright (c) 2012-2016 Synaptics Incorporated
 */

#include "RMIGPIOFunction.hpp"
#include "RMILogging.h"
#include "RMIMessages.h"
#include "LinuxCompat.h"
#include "VoodooInputMessages.h"

#define TRACKPOINT_RANGE_START      3
#define TRACKPOINT_RANGE_END        6

OSDefineMetaClassAndStructors(RMIGPIOFunction, RMIFunction)
#define super RMIFunction

bool RMIGPIOFunction::attach(IOService *provider)
{
    int error;
    
    if (!super::attach(provider)) {
        return false;
    }

    error = initialize();
    if (error)
        return false;

    return true;
}

IOReturn RMIGPIOFunction::config()
{
    /* Write Control Register values back to device */
    int error = writeBlock(getCtrlAddr(),
                           ctrl_regs, ctrl_regs_size);

    if (error) {
        IOLogError("%s: Could not write control registers at 0x%x: %d",
                   getName(), getCtrlAddr(), error);
    }
    return error;
}

int RMIGPIOFunction::mapGpios()
{
    if (!has_gpio)
        return -1;

    data_regs = reinterpret_cast<uint8_t *>(IOMalloc(register_count * sizeof(uint8_t)));
    if (!data_regs) {
        IOLogError("%s - Failed to allocate %d data registers", getName(), register_count);
        return -1;
    }
    bzero(data_regs, register_count * sizeof(uint8_t));

    unsigned int button = BTN_LEFT;
    unsigned int trackpoint_button = BTN_LEFT;
    int buttonArrLen = min(gpioled_count, TRACKPOINT_RANGE_END);
    const RmiGpioData &gpio = getGPIOData();
    setProperty("Button Count", buttonArrLen, 32);

    gpioled_key_map = reinterpret_cast<uint16_t *>(IOMalloc(buttonArrLen * sizeof(uint16_t)));
    if (!gpioled_key_map) {
        IOLogError("%s - Failed to allocate %d gpioled map memory", getName(), buttonArrLen);
        return -1;
    }
    bzero(gpioled_key_map, buttonArrLen * sizeof(gpioled_key_map[0]));

    for (int i = 0; i < buttonArrLen; i++) {
        if (!is_valid_button(i))
            continue;

        if (gpio.trackpointButtons &&
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

void RMIGPIOFunction::attention()
{
    int error = readBlock(getDataAddr(),
                          data_regs, register_count);

    if (error < 0) {
        IOLogError("Could not read %s data: %d", getName(), error);
    }

    if (has_gpio)
        reportButton();
}

void RMIGPIOFunction::reportButton()
{
    TrackpointReport relativeEvent {};
    unsigned int mask, trackpointBtns = 0, btns = 0;
    unsigned int reg_num, bit_num;
    UInt16 key_code;
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
                 notify(kHandleRMIClickpadSet, reinterpret_cast<void *>(key_down));
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

    if (numButtons > 1) {
        AbsoluteTime timestamp;
        clock_get_uptime(&timestamp);

        relativeEvent.dx = relativeEvent.dy = 0;
        relativeEvent.buttons = btns;
        relativeEvent.timestamp = timestamp;
        sendVoodooInputPacket(kIOMessageVoodooTrackpointRelativePointer, &relativeEvent);
    }

    if (hasTrackpointButtons) {
        notify(kHandleRMITrackpointButton, reinterpret_cast<void *>(trackpointBtns));
    }
}

void RMIGPIOFunction::free() {
    IOFree(gpioled_key_map, min(gpioled_count, TRACKPOINT_RANGE_END) * sizeof(uint16_t));
    IOFree(query_regs, query_regs_size * sizeof(uint8_t));
    IOFree(ctrl_regs, ctrl_regs_size * sizeof(uint8_t));
    IOFree(data_regs, register_count * sizeof(uint8_t));

    super::free();
}
