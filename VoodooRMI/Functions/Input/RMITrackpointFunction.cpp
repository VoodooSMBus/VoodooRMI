/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 */

#include "RMITrackpointFunction.hpp"
#include "RMILogging.h"
#include "RMIMessages.h"

OSDefineMetaClassAndStructors(RMITrackpointFunction, RMIFunction)

void RMITrackpointFunction::handleReport(RMITrackpointReport *report) {
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    
    TrackpointReport trackpointReport;
    trackpointReport.dx = report->dx;
    trackpointReport.dy = report->dy;
    trackpointReport.buttons = report->buttons | overwrite_buttons;
    trackpointReport.timestamp = timestamp;
    
    sendVoodooInputPacket(kIOMessageVoodooTrackpointMessage, &trackpointReport);
    if (report->dx || report->dy) {
        notify(kHandleRMITrackpoint);
    }

    IOLogDebug("Dx: %d Dy : %d, Buttons: %d", report->dx, report->dy, report->buttons);
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
