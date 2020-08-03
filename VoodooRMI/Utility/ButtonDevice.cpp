/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Simple HID device that handles buttons
 */

#include "ButtonDevice.hpp"
#include "RMIBus.hpp"

OSDefineMetaClassAndStructors(ButtonDevice, IOHIPointing);
#define super IOHIPointing

UInt32 ButtonDevice::deviceType() {
    return NX_EVS_DEVICE_TYPE_MOUSE;
}

UInt32 ButtonDevice::interfaceID() {
    return NX_EVS_DEVICE_INTERFACE_BUS_ACE;
}

IOItemCount ButtonDevice::buttonCount()
{
    return 3;
}

IOFixed ButtonDevice::resolution() {
    return (150) << 16;
};

bool ButtonDevice::start(IOService* provider) {
    if (!super::start(provider)) {
        return false;
    }
    
    registerService();
    return true;
}

void ButtonDevice::stop(IOService* provider) {
    super::stop(provider);
}

void ButtonDevice::updateButtons(int buttons) {
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    dispatchRelativePointerEvent(0, 0, buttons, now_abs);
};

void ButtonDevice::updateRelativePointer(int dx, int dy, int buttons) {
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    dispatchRelativePointerEvent(dx, dy, buttons, now_abs);
};

void ButtonDevice::updateScrollwheel(short deltaAxis1, short deltaAxis2, short deltaAxis3) {
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    dispatchScrollWheelEvent(deltaAxis1, deltaAxis2, deltaAxis3, now_abs);
    
}
