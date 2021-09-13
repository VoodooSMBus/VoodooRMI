/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F30.c
 *
 * Copyright (c) 2012-2016 Synaptics Incorporated
 */


#ifndef RMIGPIOFunction_hpp
#define RMIGPIOFunction_hpp

#include "RMIFunction.hpp"

class RMIGPIOFunction : public RMIFunction {
   OSDeclareDefaultStructors(RMIGPIOFunction)

public:
    bool attach(IOService *provider) override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = 0) override;
    void free() override;

protected:
    RelativePointerEvent relativeEvent {};

    uint8_t *query_regs {nullptr};
    uint32_t query_regs_size {0};
    uint8_t *ctrl_regs {nullptr};
    uint32_t ctrl_regs_size {0};
    uint8_t *data_regs {nullptr};
    uint32_t data_regs_size {0};

    u8 register_count {0};
    u8 gpioled_count {0};
    u16 gpioled_key_map[TRACKPOINT_RANGE_END];

    bool has_gpio {true};
    u8 numButtons {0};
    u8 clickpadIndex {0};
    bool clickpadState {false};
    bool hasTrackpointButtons {false};

    virtual inline int initialize() {return -1;};
    virtual inline bool is_valid_button(int button) {return false;};

    int config();
    int mapGpios();
    void reportButton();
};

#endif /* RMIGPIOFunction_hpp */
