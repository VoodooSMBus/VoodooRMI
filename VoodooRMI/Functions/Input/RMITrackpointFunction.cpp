/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 */

#include "RMITrackpointFunction.hpp"
#include "RMILogging.h"
#include "RMIMessages.h"

OSDefineMetaClassAndStructors(RMITrackpointFunction, RMIFunction)

#define MIDDLE_MOUSE_MASK 0x04

bool RMITrackpointFunction::shouldDiscardReport() {
    return getVoodooInput() != nullptr;
}

void RMITrackpointFunction::handleReport(RMITrackpointReport *report) {
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    
    SInt32 dx = report->dx;
    SInt32 dy = report->dy;
    UInt32 buttons = report->buttons | overwrite_buttons;

    const RmiConfiguration &conf = getConfiguration();
    
    // The highest dx/dy is lowered by subtracting by trackpointDeadzone.
    // This however does allows values below the deadzone value to still be sent, preserving control in the lower end

    dx -= signum(dx) * min(abs(dx), conf.trackpointDeadzone);
    dy -= signum(dy) * min(abs(dy), conf.trackpointDeadzone);

    // For middle button, we do not actually tell macOS it's been pressed until it's been released and we didn't scroll
    // We first say that it's been pressed internally - but if we scroll at all, then instead we say we scroll
    if (buttons & MIDDLE_MOUSE_MASK && !isScrolling) {
        if (dx || dy) {
            isScrolling = true;
            middlePressed = false;
        } else {
            middlePressed = true;
        }
    }
    
    IOService *voodooInputInstance = const_cast<IOService *>(getVoodooInput());

    // When middle button is released, if we registered a middle press w/o scrolling, send middle click as a seperate packet
    // Otherwise just turn scrolling off and remove middle buttons from packet
    if (!(buttons & MIDDLE_MOUSE_MASK)) {
        if (middlePressed) {
            dispatchPointerEvent(voodooInputInstance, dx, dy, MIDDLE_MOUSE_MASK, timestamp);
        }
        
        middlePressed = false;
        isScrolling = false;
    }

    buttons &= ~MIDDLE_MOUSE_MASK;

    // Must multiply first then divide so we don't multiply by zero
    if (isScrolling) {
        SInt32 scrollY = (SInt32)((SInt64)-dy * conf.trackpointScrollYMult / DEFAULT_MULT);
        SInt32 scrollX = (SInt32)((SInt64)-dx * conf.trackpointScrollXMult / DEFAULT_MULT);
        
        dispatchScrollEvent(voodooInputInstance, scrollY, scrollX, timestamp);
    } else {
        SInt32 mulDx = (SInt32)((SInt64)dx * conf.trackpointMult / DEFAULT_MULT);
        SInt32 mulDy = (SInt32)((SInt64)dy * conf.trackpointMult / DEFAULT_MULT);
        
        dispatchPointerEvent(voodooInputInstance, mulDx, mulDy, buttons, timestamp);
    }

    if (dx || dy) {
        notify(kHandleRMITrackpoint);
    }

    IOLogDebug("Dx: %d Dy : %d, Buttons: %d", dx, dy, buttons);
}

int RMITrackpointFunction::signum(int value)
{
    if (value > 0) return 1;
    if (value < 0) return -1;
    return 0;
}

void RMITrackpointFunction::dispatchScrollEvent(IOService *voodooInputInstance,
                                                short delta1,
                                                short delta2,
                                                AbsoluteTime timestamp) {
    scrollEvent.deltaAxis1 = delta1;
    scrollEvent.deltaAxis2 = delta2;
    scrollEvent.deltaAxis3 = 0; // Never used
    scrollEvent.timestamp = timestamp;
    
    messageClient(kIOMessageVoodooTrackpointScrollWheel, voodooInputInstance, &scrollEvent, sizeof(ScrollWheelEvent));
}

void RMITrackpointFunction::dispatchPointerEvent(IOService *voodooInputInstance,
                                                 int dx,
                                                 int dy,
                                                 int buttons,
                                                 AbsoluteTime timestamp) {
    relativeEvent.dx = dx;
    relativeEvent.dy = dy;
    relativeEvent.buttons = buttons;
    relativeEvent.timestamp = timestamp;
    
    messageClient(kIOMessageVoodooTrackpointRelativePointer, voodooInputInstance, &relativeEvent, sizeof(RelativePointerEvent));
}

IOReturn RMITrackpointFunction::message(UInt32 type, IOService *provider, void *argument) {
    switch (type) {
        case kHandleRMITrackpointButton:
            // This message originates in RMIBus::Notify, which sends an unsigned int
            overwrite_buttons = (unsigned int)((intptr_t) argument);
            handleReport(&emptyReport);
            break;
    }
    
    return kIOReturnSuccess;
}
