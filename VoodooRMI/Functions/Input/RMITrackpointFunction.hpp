/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 */

#ifndef RMITrackpointFunction_hpp
#define RMITrackpointFunction_hpp

#include "RMIFunction.hpp"
#include <VoodooInputMessages.h>

struct RMITrackpointReport {
    SInt32 dx;
    SInt32 dy;
    UInt32 buttons;
};

class RMITrackpointFunction : public RMIFunction {
   OSDeclareDefaultStructors(RMITrackpointFunction)

    bool shouldDiscardReport();
    void handleReport(RMITrackpointReport *report);
    
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;

public:
    RMITrackpointReport report {};

private:
    RelativePointerEvent relativeEvent {};
    ScrollWheelEvent scrollEvent {};
    
    // Used when sending buttons from other functions
    RMITrackpointReport emptyReport {};
    
    bool isScrolling;
    bool middlePressed;
    unsigned int overwrite_buttons;
    
    int signum(int value);
    void dispatchScrollEvent (IOService *, short, short, AbsoluteTime);
    void dispatchPointerEvent (IOService *, int, int, int, AbsoluteTime);
};

#endif /* RMITrackpointFunction_hpp */
