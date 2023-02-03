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
    IOReturn config() override;
    void attention() override;
    void free() override;

protected:
    uint8_t *query_regs {nullptr};
    uint8_t query_regs_size {0};
    uint8_t *ctrl_regs {nullptr};
    uint8_t ctrl_regs_size {0};
    uint8_t *data_regs {nullptr};

    UInt8 register_count {0};
    UInt8 gpioled_count {0};
    UInt16 *gpioled_key_map {nullptr};

    bool has_gpio {true};
    UInt8 numButtons {0};
    UInt8 clickpadIndex {0};
    bool clickpadState {false};
    bool hasTrackpointButtons {false};

    virtual inline int initialize() {return -1;};
    virtual inline bool is_valid_button(int button) {return false;};

    int mapGpios();
    void reportButton();
};

#endif /* RMIGPIOFunction_hpp */
