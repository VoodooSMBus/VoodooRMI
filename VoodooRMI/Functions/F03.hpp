/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F03.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#ifndef F03_hpp
#define F03_hpp

#include "../RMIBus.hpp"

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

#define PSMOUSE_OOB_EXTRA_BTNS        0x01

class F03 : public RMIFunction {
    OSDeclareDefaultStructors(F03)
    

public:
    bool init(OSDictionary *dictionary) override;
    bool attach(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;

private:
    // F03 Data
    unsigned int overwrite_buttons;
    
    u8 device_count;
    u8 rx_queue_length;


};

#endif /* F03_hpp */
