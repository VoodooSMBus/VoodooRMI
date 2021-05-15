/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F03.c
 * https://github.com/torvalds/linux/blob/master/drivers/input/mouse/trackpoint.c
 * https://github.com/torvalds/linux/blob/master/drivers/input/mouse/psmouse-base.c
 *
 * Synaptic RMI4:
 * Copyright (c) 2015-2016 Red Hat
 * Copyright (c) 2015 Lyude Paul <thatslyude@gmail.com>
 */

#ifndef F03_hpp
#define F03_hpp

#include "../RMIBus.hpp"
#include "../Utility/PS2.hpp"
#include "../Utility/Configuration.hpp"
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>

#define PSMOUSE_CMD_ENABLE 0x00f4

/*
 * These constants are from the TrackPoint System
 * Engineering documentation Version 4 from IBM Watson
 * research:
 *    http://wwwcssrv.almaden.ibm.com/trackpoint/download.html
 */

#define TP_COMMAND        0xE2    /* Commands start with this */

#define TP_READ_ID        0xE1    /* Sent for device identification */

/*
 * Valid first byte responses to the "Read Secondary ID" (0xE1) command.
 * 0x01 was the original IBM trackpoint, others implement very limited
 * subset of trackpoint features.
 */
#define TP_VARIANT_IBM        0x01
#define TP_VARIANT_ALPS        0x02
#define TP_VARIANT_ELAN        0x03
#define TP_VARIANT_NXP        0x04

/*
 * Commands
 */
#define TP_RECALIB        0x51    /* Recalibrate */
#define TP_POWER_DOWN        0x44    /* Can only be undone through HW reset */
#define TP_EXT_DEV        0x21    /* Determines if external device is connected (RO) */
#define TP_EXT_BTN        0x4B    /* Read extended button status */
#define TP_POR            0x7F    /* Execute Power on Reset */
#define TP_POR_RESULTS        0x25    /* Read Power on Self test results */
#define TP_DISABLE_EXT        0x40    /* Disable external pointing device */
#define TP_ENABLE_EXT        0x41    /* Enable external pointing device */

/*
 * Mode manipulation
 */
#define TP_SET_SOFT_TRANS    0x4E    /* Set mode */
#define TP_CANCEL_SOFT_TRANS    0xB9    /* Cancel mode */
#define TP_SET_HARD_TRANS    0x45    /* Mode can only be set */

#define RMI_F03_RX_DATA_OFB        0x01
#define RMI_F03_OB_SIZE            2

#define RMI_F03_OB_OFFSET        2
#define RMI_F03_OB_DATA_OFFSET        1
#define RMI_F03_OB_FLAG_TIMEOUT        BIT(6)
#define RMI_F03_OB_FLAG_PARITY        BIT(7)

#define RMI_F03_DEVICE_COUNT        0x07
#define RMI_F03_BYTES_PER_DEVICE    0x07
#define RMI_F03_BYTES_PER_DEVICE_SHIFT    4
#define RMI_F03_QUEUE_LENGTH        0x0F

// trackpoint.h
/*
 * Commands
 */
#define TP_RECALIB        0x51    /* Recalibrate */
#define TP_POWER_DOWN        0x44    /* Can only be undone through HW reset */
#define TP_EXT_DEV        0x21    /* Determines if external device is connected (RO) */
#define TP_EXT_BTN        0x4B    /* Read extended button status */
#define TP_POR            0x7F    /* Execute Power on Reset */
#define TP_POR_RESULTS        0x25    /* Read Power on Self test results */
#define TP_DISABLE_EXT        0x40    /* Disable external pointing device */
#define TP_ENABLE_EXT        0x41    /* Enable external pointing device */

/*
 * Mode manipulation
 */
#define TP_SET_SOFT_TRANS    0x4E    /* Set mode */
#define TP_CANCEL_SOFT_TRANS    0xB9    /* Cancel mode */
#define TP_SET_HARD_TRANS    0x45    /* Mode can only be set */


/*
 * Register oriented commands/properties
 */
#define TP_WRITE_MEM        0x81

/* Power on Self Test Results */
#define TP_POR_SUCCESS        0x3B

#define MAKE_PS2_CMD(params, results, cmd) ((params<<12) | (results<<8) | (cmd))

static const char * const trackpoint_variants[] = {
    [TP_VARIANT_IBM]    = "IBM",
    [TP_VARIANT_ALPS]   = "ALPS",
    [TP_VARIANT_ELAN]   = "Elan",
    [TP_VARIANT_NXP]    = "NXP",
};

class F03 : public RMIFunction {
    OSDeclareDefaultStructors(F03)
    
public:
    bool attach(IOService *provider) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    
private:
    
    RMIBus *rmiBus;
    IOWorkLoop *work_loop;
    IOCommandGate *command_gate;
    
    IOService **voodooTrackpointInstance {nullptr};
    IOTimerEventSource *timer {nullptr};
    RelativePointerEvent *relativeEvent {nullptr};
    ScrollWheelEvent *scrollEvent {nullptr};
    
    bool isScrolling;
    bool middlePressed;
    
    // trackpoint
    u8 vendor {0};
    
    // ps2
    unsigned int flags, cmdcnt;
    u8 cmdbuf[8];
    u8 status {0};
    u8 reinit{0}, maxReinit{3};
    
    // Packet storage
    u8 emptyPkt[3];
    u8 databuf[3];
    u8 index;
    
    // F03 Data
    unsigned int overwrite_buttons;
    
    u8 device_count;
    u8 rx_queue_length;

    IOWorkLoop* getWorkLoop();
    
    int rmi_f03_pt_write(unsigned char val);
    int ps2DoSendbyteGated(u8 byte, uint64_t timeout);
    int ps2CommandGated(u8 *param, unsigned int *command);
    int ps2Command(u8 *param, unsigned int command);
    void handleByte(u8);
    void initPS2();
    void initPS2Interrupt(OSObject *owner, IOTimerEventSource *timer);
    // TODO: Move to math file as long as with abs in rmi_driver.h
    int signum(int value);
    
    void handlePacket(u8 *packet);
};

#endif /* F03_hpp */
