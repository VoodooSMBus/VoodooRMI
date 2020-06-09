/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Simple HID device that handles buttons
 */

#ifndef ButtonDevice_hpp
#define ButtonDevice_hpp

#include <IOKit/hidsystem/IOHIPointing.h>
#include <IOKit/hidsystem/IOHIDParameter.h>

class ButtonDevice : public IOHIPointing {
    OSDeclareDefaultStructors(ButtonDevice)
    
protected:
    virtual IOItemCount buttonCount() override;
    virtual IOFixed resolution() override;
    
public:
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    
    virtual UInt32 deviceType() override;
    virtual UInt32 interfaceID() override;
    
    void updateButtons(int buttons);
};

#endif /* ButtonDevice_hpp */
