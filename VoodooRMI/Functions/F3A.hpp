/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F3A.c
 *
 * Copyright (c) 2012-2020 Synaptics Incorporated
 */

#ifndef F3A_hpp
#define F3A_hpp

#include <RMIBus.hpp>

#define RMI_F3A_MAX_GPIO_COUNT       128
#define RMI_F3A_MAX_REG_SIZE        DIV_ROUND_UP(RMI_F3A_MAX_GPIO_COUNT, 8)

#define RMI_F3A_GPIO_COUNT          0x7F
#define RMI_F3A_DATA_REGS_MAX_SIZE   RMI_F3A_MAX_REG_SIZE

class F3A : public RMIFunction {
    OSDeclareDefaultStructors(F3A)
    
public:
    bool attach(IOService *provider) override;
    bool start(IOService *provider) override;
//    void stop(IOService *providerr) override;
    void free() override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;

private:
    RMIBus *rmiBus;
    IOService **voodooTrackpointInstance{nullptr};
    RelativePointerEvent *relativeEvent {nullptr};
    
    u16 *gpioled_key_map;
    u8 gpioCount {0};
    u8 registerCount {0};
    u8 data_regs [RMI_F3A_DATA_REGS_MAX_SIZE];
    u8 numButtons {0};
    u8 clickpadIndex {0};
    bool clickpadState {false};
    
    bool mapGpios(u8 *query1_regs, u8 *ctrl1_regs);
    inline bool is_valid_button(int button, u8 *query1_regs, u8 *ctrl1_regs) {
        return (query1_regs[0] & BIT(button)) && !(ctrl1_regs[0] & BIT(button));
    }
};

#endif /* F3A_hpp */
