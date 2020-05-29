/*
 * VoodooSMBusDeviceNub.hpp
 * SMBus Controller Driver for macOS X
 *
 * Copyright (c) 2019 Leonard Kleinhans <leo-labs>
 *
 */

#ifndef VoodooSMBus_h
#define VoodooSMBus_h

#include "../LinuxCompat.h"

class VoodooSMBusControllerDriver;
class VoodooSMBusSlaveDevice;
class VoodooSMBusDeviceNub : public IOService {
    OSDeclareDefaultStructors(VoodooSMBusDeviceNub);
    
public:
    bool init() override;
    bool attach(IOService* provider, UInt8 address);
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    void free(void) override;
    
    void handleHostNotify();
    void setSlaveDeviceFlags(unsigned short flags);
    
    IOReturn writeByteData(u8 command, u8 value);
    IOReturn readBlockData(u8 command, u8 *values);
    IOReturn writeByte(u8 value);
    IOReturn writeBlockData(u8 command, u8 length, const u8 *values);
    IOReturn readByteData(u8 command);
    
private:
    VoodooSMBusControllerDriver* controller;
    void releaseResources();
    VoodooSMBusSlaveDevice* slave_device;
    void handleHostNotifyThreaded();
};

#endif /* VoodooSMBus_h */
