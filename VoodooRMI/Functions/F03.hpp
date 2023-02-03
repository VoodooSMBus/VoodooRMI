/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F03.c
 *
 * Synaptic RMI4:
 * Copyright (c) 2015-2016 Red Hat
 * Copyright (c) 2015 Lyude Paul <thatslyude@gmail.com>
 */

#ifndef F03_hpp
#define F03_hpp

#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <RMITrackpointFunction.hpp>

class F03 : public RMITrackpointFunction {
    OSDeclareDefaultStructors(F03)
    
public:
    bool attach(IOService *provider) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    
private:
    IOWorkLoop *work_loop;
    IOCommandGate *command_gate;
    IOTimerEventSource *timer {nullptr};
    
    // trackpoint
    UInt8 vendor {0};
    
    // ps2
    unsigned int flags, cmdcnt;
    UInt8 cmdbuf[8];
    UInt8 status {0};
    UInt8 reinit {0}, maxReinit {3};
    
    // Packet storage
    UInt8 emptyPkt[3] {0};
    UInt8 databuf[3] {0};
    UInt8 index;
    
    // F03 Data
    UInt8 device_count;
    UInt8 rx_queue_length;

    IOWorkLoop* getWorkLoop();
    
    int rmi_f03_pt_write(unsigned char val);
    int ps2DoSendbyteGated(UInt8 byte, uint64_t timeout);
    int ps2CommandGated(UInt8 *param, unsigned int *command);
    int ps2Command(UInt8 *param, unsigned int command);
    void handleByte(UInt8);
    void initPS2();
    void initPS2Interrupt(OSObject *owner, IOTimerEventSource *timer);
    
    void handlePacket(UInt8 *packet);
};

#endif /* F03_hpp */
