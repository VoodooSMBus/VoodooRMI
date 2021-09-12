/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F30.c
 *
 * Copyright (c) 2012-2016 Synaptics Incorporated
 */

#ifndef F30_hpp
#define F30_hpp

#include <RMIGPIOFunction.hpp>

#define RMI_F30_QUERY_SIZE            2

/* Defs for Query 0 */
#define RMI_F30_EXTENDED_PATTERNS       0x01
#define RMI_F30_HAS_MAPPABLE_BUTTONS    BIT(1)
#define RMI_F30_HAS_LED                 BIT(2)
#define RMI_F30_HAS_GPIO                BIT(3)
#define RMI_F30_HAS_HAPTIC              BIT(4)
#define RMI_F30_HAS_GPIO_DRV_CTL        BIT(5)
#define RMI_F30_HAS_MECH_MOUSE_BTNS     BIT(6)

/* Defs for Query 1 */
#define RMI_F30_GPIO_LED_COUNT          0x1F

/* Defs for Control Registers */
#define RMI_F30_CTRL_1_GPIO_DEBOUNCE    0x01
#define RMI_F30_CTRL_1_HALT             BIT(4)
#define RMI_F30_CTRL_1_HALTED           BIT(5)
#define RMI_F30_CTRL_10_NUM_MECH_MOUSE_BTNS    0x03

#define RMI_F30_CTRL_MAX_REGS           32
#define RMI_F30_CTRL_MAX_BYTES          RMI_F30_CTRL_MAX_REGS / 8
#define RMI_F30_CTRL_MAX_REG_BLOCKS     11

#define RMI_F30_CTRL_REGS_MAX_SIZE (RMI_F30_CTRL_MAX_BYTES        \
    + 1                \
    + RMI_F30_CTRL_MAX_BYTES    \
    + RMI_F30_CTRL_MAX_BYTES    \
    + RMI_F30_CTRL_MAX_BYTES    \
    + 6                \
    + RMI_F30_CTRL_MAX_REGS        \
    + RMI_F30_CTRL_MAX_REGS        \
    + RMI_F30_CTRL_MAX_BYTES    \
    + 1                \
    + 1)

struct rmi_f30_ctrl_data {
    int address;
    int length;
    uint8_t *regs;
};

class F30 : public RMIGPIOFunction {
    OSDeclareDefaultStructors(F30)
    
public:
    bool init(OSDictionary *dictionary) override;
    bool start(IOService *provider) override;
    void free() override;

private:    
    /* Query Data */
    bool has_extended_pattern;
    bool has_mappable_buttons;
    bool has_led;
    bool has_haptic;
    bool has_gpio_driver_control;
    bool has_mech_mouse_btns;
    
    /* Control Register Data */
    rmi_f30_ctrl_data ctrl[RMI_F30_CTRL_MAX_REG_BLOCKS];

    int initialize() override;
    void rmi_f30_set_ctrl_data(rmi_f30_ctrl_data *ctrl,
                               int *ctrl_addr, int len, u8 **reg);
    bool is_valid_button(int button) override;
};

#endif /* F30_hpp */
