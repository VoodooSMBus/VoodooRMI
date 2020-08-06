 /* SPDX-License-Identifier: GPL-2.0-only
  * Copyright (c) 2020 Zhen
  * Ported to macOS from linux kernel, original source at
  * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_i2c.c
  * https://github.com/torvalds/linux/blob/master/drivers/hid/hid-rmi.c
  * and code written by Alexandre, CoolStar and Kishor Prins
  * https://github.com/VoodooI2C/VoodooI2CSynaptics
  *
  * Copyright (c) 2011-2016 Synaptics Incorporated
  * Copyright (c) 2011 Unixphere
  */

#ifndef RMISMBus_h
#define RMISMBus_h

#include "RMITransport.hpp"
#include "VoodooI2CDeviceNub.hpp"
#include <IOKit/IOTimerEventSource.h>

#define RMI_MOUSE_REPORT_ID        0x01 /* Mouse emulation Report */
#define RMI_WRITE_REPORT_ID        0x09 /* Output Report */
#define RMI_READ_ADDR_REPORT_ID        0x0a /* Output Report */
#define RMI_READ_DATA_REPORT_ID        0x0b /* Input Report */
#define RMI_ATTN_REPORT_ID        0x0c /* Input Report */
#define RMI_SET_RMI_MODE_REPORT_ID    0x0f /* Feature Report */

#define RMI_PAGE_SELECT_REGISTER 0xff
#define RMI_I2C_PAGE(addr) (((addr) >> 8) & 0xff)

#define HID_DATA_REGISTER       0x0023
#define HID_OUTPUT_REGISTER     0x0025
#define HID_COMMAND_REGISTER    0x0022

#define INTERRUPT_SIMULATOR_INTERVAL 200
#define INTERRUPT_SIMULATOR_TIMEOUT 5
#define INTERRUPT_SIMULATOR_TIMEOUT_BUSY 2
#define INTERRUPT_SIMULATOR_TIMEOUT_IDLE 50

#define HID_I2C_DSM_HIDG "3cdff6f7-4267-4555-ad05-b30a3d8938de"

enum rmi_mode_type {
    RMI_MODE_OFF = 0,
    RMI_MODE_ATTN_REPORTS = 1,
    RMI_MODE_NO_PACKED_ATTN_REPORTS = 2,
};

class RMII2C : public RMITransport {
    OSDeclareDefaultStructors(RMII2C);
    typedef IOService super;

public:
    const char* name;

    RMII2C *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService* device) APPLE_KEXT_OVERRIDE;
    IOReturn setPowerState(unsigned long powerState, IOService *whatDevice) APPLE_KEXT_OVERRIDE;
    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) APPLE_KEXT_OVERRIDE;

    int reset() APPLE_KEXT_OVERRIDE;
    int readBlock(u16 rmiaddr, u8 *databuff, size_t len) APPLE_KEXT_OVERRIDE;
    int blockWrite(u16 rmiaddr, u8 *buf, size_t len) APPLE_KEXT_OVERRIDE;

private:
    bool ready {false};
    UInt16 hid_descriptor_register;
    int page {0};
    int reportMode {RMI_MODE_NO_PACKED_ATTN_REPORTS};

    IOACPIPlatformDevice *acpi_device {nullptr};
    VoodooI2CDeviceNub *device_nub {nullptr};

    IOLock *page_mutex {nullptr};

    IOWorkLoop* work_loop;
    IOCommandGate* command_gate;
    IOTimerEventSource* interrupt_simulator;
    IOInterruptEventSource* interrupt_source;

    void interruptOccured(OSObject* owner, IOInterruptEventSource* src, int intCount);
    void simulateInterrupt(OSObject* owner, IOTimerEventSource* timer);
    void notifyClient();

    IOReturn getHIDDescriptorAddress();

    int rmi_set_page(u8 page);
    int rmi_set_mode(u8 mode);

    void releaseResources();

    void startInterrupt();
    void stopInterrupt();
};

#endif
