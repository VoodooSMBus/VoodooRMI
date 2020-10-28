//
//  F3A.cpp
//  VoodooRMI
//
//  Created by Avery Black on 9/20/20.
//  Copyright © 2020 1Revenger1. All rights reserved.
//

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
    
    u8 query[F3A_QUERY_SIZE];
    memset (query, 0, sizeof(query));
    IOReturn status = rmiBus->readBlock(fn_descriptor->query_base_addr, query, F3A_QUERY_SIZE);
    if (status != 0) {
        IOLogError("F3A - Failed to read Query Registers!");
    }
    
#ifdef DEBUG
    // Both devices that I've had tested both had a single button for the clickpad
    /*
     * QRY has 4 registers
     * QRY_0 = (0x03) Flags? // Num of GPIO
     * QRY_1 = (0x04) GPIO Count & 0x1F? GPIO Mask?
     * QRY_2 = ???? (0x00 OR 0x04)
     * QRY_3 = ???? (0x00 OR 0x04)
     */
    
    for(int i = 0; i < F3A_QUERY_SIZE; i++) {
        IOLogDebug("F3A QRY Register %u: %x", i, query[i]);
    }
#endif // DEBUG
    if (!mapGpios(query)) {
        return false;
    }
    
    
    u8 ctrl[F3A_CTRL_SIZE];
    memset (ctrl, 0, sizeof(ctrl));
    // CTRL size is 99% likely to be variable, need to figure out how to figure out length and stuff though
    status = rmiBus->readBlock(fn_descriptor->control_base_addr, ctrl, F3A_CTRL_SIZE);
    if (status != 0) {
        IOLogError("F3A - Failed to read Ctrl Registers!");
    }
    
#ifdef DEBUG
    /*
     * CTRL has 6? registers (could be variable)
     * CTRL_0 = ??? (0x02)
     * CTRL_1 = ??? (0x00)
     * CTRL_2 = ??? (0x00 OR 0x04)
     * CTRL_3 = ??? (0x00)
     * CTRL_4 = ??? (0x00)
     * CTRL_5 = ??? (0x0e)
     */
    
    for (int i = 0; i < F3A_CTRL_SIZE; i++) {
        IOLogDebug("F3A CTRL Reg %u: %x", i, ctrl[i]);
    }
#endif //DEBUG
    
    return true;
}

bool F3A::start(IOService *provider)
{
    if (numButtons != 1) {
        setProperty("VoodooTrackpointSupported", kOSBooleanTrue);
    }
    
    registerService();
    return super::start(provider);
}

bool F3A::mapGpios(u8 *qryRegs)
{
    if (!qryRegs)
        return false;
    
    unsigned int button = BTN_LEFT;
    unsigned int trackpoint_button = BTN_LEFT;
    gpioCount = qryRegs[0];
    setProperty("Button Count", gpioCount, 32);
    
    gpioled_key_map = reinterpret_cast<uint16_t *>(IOMalloc(gpioCount * sizeof(gpioled_key_map[0])));
    if (!gpioled_key_map) {
        IOLogError("Could not create gpioled_key_map!");
        return false;
    }
    
    memset(gpioled_key_map, 0, gpioCount * sizeof(gpioled_key_map[0]));
    
    for (int i = 0; i < gpioCount; i++) {
        if (!(BIT(i) & qryRegs[1]))
            continue;
        
        // I don't know if this logic holds true or not for trackpoint buttons, this is just from F3A
        if (i >= TRACKPOINT_RANGE_START && i < TRACKPOINT_RANGE_END) {
            IOLogDebug("F30: Found Trackpoint button %d\n", button);
            gpioled_key_map[i] = trackpoint_button++;
        } else {
            IOLogDebug("F30: Found Button %d", button);
            gpioled_key_map[i] = button++;
            numButtons++;
            clickpadIndex = i;
        }
    }
    
    setProperty("Trackpoint Buttons through F3A", trackpoint_button != BTN_LEFT);
    setProperty("Clickpad", numButtons == 1);
    return true;
}


IOReturn F3A::message(UInt32 type, IOService *provider, void *argument)
{
    u8 reg = 0;
    int error = 0;
    unsigned int mask, /*trackpointBtns = 0,*/ btns = 0;
    u16 key_code;
    bool key_down;
    
    switch (type) {
        case kHandleRMIAttention:
            // No point doing work
            if (!voodooTrackpointInstance && numButtons != 1)
                return kIOReturnIOError;
            
            error = rmiBus->readBlock(fn_descriptor->data_base_addr,
                                          &reg, 1);
            
            if (error < 0) {
                IOLogError("Could not read F3A data: 0x%x", error);
            }
            
            IOLogVerbose("F3A Attention! DataReg: %u", reg);
            
            for (int i = 0; i < gpioCount; i++) {
                if (gpioled_key_map[i] == KEY_RESERVED)
                    continue;
                
                // F3A appears to pull bits down when pressed. High means not pressed
                key_down = !(BIT(i) & reg);
                key_code = gpioled_key_map[i];
                mask = key_down << (key_code - 1);
                
                IOLogVerbose("Key %u is %s", key_code, key_down ? "Down": "Up");
                
                if (i >= TRACKPOINT_RANGE_START &&
                    i < TRACKPOINT_RANGE_END) {
                    IOLogInfo("Trackpoint button?");
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
                
                if (numButtons > 1) {
                    AbsoluteTime timestamp;
                    clock_get_uptime(&timestamp);
                    
                    relativeEvent.dx = relativeEvent.dy = 0;
                    relativeEvent.buttons = btns;
                    relativeEvent.timestamp = timestamp;
                    
                    messageClient(kIOMessageVoodooTrackpointRelativePointer, voodooTrackpointInstance, &relativeEvent, sizeof(RelativePointerEvent));
                }
            }
            
            break;
        default:
            return super::message(type, provider, argument);
    }
    
    return kIOReturnSuccess;
}

bool F3A::handleOpen(IOService *forClient, IOOptionBits options, void *arg)
{
    if (forClient && forClient->getProperty(VOODOO_TRACKPOINT_IDENTIFIER)) {
        voodooTrackpointInstance = forClient;
        voodooTrackpointInstance->retain();

        return true;
    }
    
    return super::handleOpen(forClient, options, arg);
}

void F3A::handleClose(IOService *forClient, IOOptionBits options)
{
    OSSafeReleaseNULL(voodooTrackpointInstance);
    super::handleClose(forClient, options);
}

void F3A::free()
{
    clearDesc();
    if (gpioled_key_map) {
        IOFree(gpioled_key_map, gpioCount * sizeof(gpioled_key_map[0]));
    }
    
    super::free();
}
