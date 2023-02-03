/* SPDX-License-Identifier: GPL-2.0-only
 * RMI4 Messages
 *
 * Copyright (c) 2023 Avery Black
 */

#ifndef RMIMessages_h
#define RMIMessages_h

// Message types defined by ApplePS2Keyboard
enum {
    // from keyboard to mouse/trackpad
    kKeyboardSetTouchStatus = iokit_vendor_specific_msg(100),   // set disable/enable trackpad (data is bool*)
    kKeyboardGetTouchStatus = iokit_vendor_specific_msg(101),   // get disable/enable trackpad (data is bool*)
    kKeyboardKeyPressTime = iokit_vendor_specific_msg(110),      // notify of timestamp a non-modifier key was pressed (data is uint64_t*)
    // From SMBus to PS2 Trackpad
    kPS2M_SMBusStart = iokit_vendor_specific_msg(152),          // Reset, disable PS2 comms to not interfere with SMBus comms
};

// RMI Bus message types
enum {
    kHandleRMIAttention = iokit_vendor_specific_msg(2046),
    kHandleRMIClickpadSet = iokit_vendor_specific_msg(2047),
    kHandleRMITrackpoint = iokit_vendor_specific_msg(2050),
    kHandleRMITrackpointButton = iokit_vendor_specific_msg(2051),
};


#endif /* RMIMessages_h */
