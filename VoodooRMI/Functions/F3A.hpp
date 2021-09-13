/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F3A.c
 *
 * Copyright (c) 2012-2020 Synaptics Incorporated
 */

#ifndef F3A_hpp
#define F3A_hpp

#include <RMIGPIOFunction.hpp>

#define RMI_F3A_MAX_GPIO_COUNT       128
#define RMI_F3A_MAX_REG_SIZE        DIV_ROUND_UP(RMI_F3A_MAX_GPIO_COUNT, 8)

#define RMI_F3A_GPIO_COUNT          0x7F
#define RMI_F3A_DATA_REGS_MAX_SIZE   RMI_F3A_MAX_REG_SIZE

class F3A : public RMIGPIOFunction {
    OSDeclareDefaultStructors(F3A)
    
public:
    bool start(IOService *provider) override;

private:    
    int initialize() override;
    bool is_valid_button(int button) override;
};

#endif /* F3A_hpp */
