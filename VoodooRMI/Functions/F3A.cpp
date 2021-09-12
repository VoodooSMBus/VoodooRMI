/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/F3A.c
 *
 * Copyright (c) 2012-2020 Synaptics Incorporated
 */

#include "F3A.hpp"

OSDefineMetaClassAndStructors(F3A, RMIGPIOFunction)
#define super RMIGPIOFunction

bool F3A::init(OSDictionary *dictionary) {
    if (!super::init(dictionary))
        return false;

    query_regs = reinterpret_cast<uint8_t *>(IOMalloc(RMI_F3A_MAX_REG_SIZE * sizeof(uint8_t)));
    ctrl_regs = reinterpret_cast<uint8_t *>(IOMalloc(RMI_F3A_MAX_REG_SIZE * sizeof(uint8_t)));
    data_regs = reinterpret_cast<uint8_t *>(IOMalloc(RMI_F3A_DATA_REGS_MAX_SIZE * sizeof(uint8_t)));

    if (!(query_regs && ctrl_regs && data_regs))
        return false;

    memset(query_regs, 0, RMI_F3A_MAX_REG_SIZE * sizeof(uint8_t));
    memset(ctrl_regs, 0, RMI_F3A_MAX_REG_SIZE * sizeof(uint8_t));
    memset(data_regs, 0, RMI_F3A_DATA_REGS_MAX_SIZE * sizeof(uint8_t));

    return true;
}

void F3A::free() {
    IOFree(query_regs, RMI_F3A_MAX_REG_SIZE * sizeof(uint8_t));
    IOFree(ctrl_regs, RMI_F3A_MAX_REG_SIZE * sizeof(uint8_t));
    IOFree(data_regs, RMI_F3A_DATA_REGS_MAX_SIZE * sizeof(uint8_t));

    super::free();
}

int F3A::initialize()
{
    int error;

    error = bus->read(desc.query_base_addr, query_regs);
    if (error) {
        IOLogError("%s - Failed to read general info register: %d", getName(), error);
        return error;
    }

    gpioled_count = query_regs[0] & RMI_F3A_GPIO_COUNT;
    registerCount = DIV_ROUND_UP(gpioled_count, 8);

    /* Query1 -> gpio exist */
    error = bus->readBlock(desc.query_base_addr + 1, query_regs + 1, registerCount);
    if (error) {
        IOLogError("%s - Failed to read query1 registers: %d", getName(), error);
        return error;
    }

    ctrl_regs_size = registerCount + 1;

    /* Ctrl1 -> gpio direction */
    error = bus->readBlock(desc.control_base_addr, ctrl_regs, ctrl_regs_size);
    if (error) {
        IOLogError("%s - Failed to read control registers: %d", getName(), error);
        return error;
    }
    setProperty("Control register 0", ctrl_regs[0], 8);

    has_gpio = true;
    if (has_gpio) {
        error = mapGpios();
        if (error) {
            IOLogError("%s - Failed to map GPIO: %d", getName(), error);
            return error;
        }
    }
    return 0;
}

bool F3A::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    registerService();
    return true;
}

bool F3A::is_valid_button(int button)
{
    int byte_position = button >> 3;
    int bit_position = button & 0x07;

    /* gpio exist && direction input */
    // was simplified as
    // return (query1_regs[0] & BIT(button)) && !(ctrl1_regs[0] & BIT(button));
    return !(ctrl_regs[byte_position + 1] & BIT(bit_position)) &&
            (query_regs[byte_position + 1] & BIT(bit_position));
}
