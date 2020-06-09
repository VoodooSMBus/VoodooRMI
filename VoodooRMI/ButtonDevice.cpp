//
//  ButtonDevice.cpp
//  VoodooRMI
//
//  Created by Gwy on 6/9/20.
//  Copyright © 2020 1Revenger1. All rights reserved.
//

#include "ButtonDevice.hpp"

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
