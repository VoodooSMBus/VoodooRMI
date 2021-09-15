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

int F3A::initialize()
{
    int error;
    uint8_t temp;

    error = bus->read(desc.query_base_addr, &temp);
    if (error) {
        IOLogError("%s - Failed to read general info register: %d", getName(), error);
        return error;
    }

    gpioled_count = temp & RMI_F3A_GPIO_COUNT;
    register_count = DIV_ROUND_UP(gpioled_count, 8);

    query_regs_size = register_count + 1;
    ctrl_regs_size = register_count + 1;

    query_regs = reinterpret_cast<uint8_t *>(IOMalloc(query_regs_size * sizeof(uint8_t)));
    if (!query_regs) {
        IOLogError("%s - Failed to allocate %d query registers", getName(), query_regs_size);
        return -1;
    }
    bzero(query_regs, query_regs_size * sizeof(uint8_t));

    ctrl_regs = reinterpret_cast<uint8_t *>(IOMalloc(ctrl_regs_size * sizeof(uint8_t)));
    if (!ctrl_regs) {
        IOLogError("%s - Failed to allocate %d control registers", getName(), ctrl_regs_size);
        return -1;
    }
    bzero(ctrl_regs, ctrl_regs_size * sizeof(uint8_t));

    /* Query1 -> gpio exist */
    error = bus->readBlock(desc.query_base_addr, query_regs, query_regs_size);
    if (error) {
        IOLogError("%s - Failed to read query1 registers: %d", getName(), error);
        return error;
    }

    /* Ctrl1 -> gpio direction */
    error = bus->readBlock(desc.control_base_addr, ctrl_regs, ctrl_regs_size);
    if (error) {
        IOLogError("%s - Failed to read control registers: %d", getName(), error);
        return error;
    }
#ifdef DEBUG
    setProperty("Control register 0", ctrl_regs[0], 8);
#endif
    error = mapGpios();
    if (error) {
        IOLogError("%s - Failed to map GPIO: %d", getName(), error);
        return error;
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
    // was simplified as:
    // return (query1_regs[0] & BIT(button)) && !(ctrl1_regs[0] & BIT(button));
    return !(ctrl_regs[byte_position + 1] & BIT(bit_position)) &&
            (query_regs[byte_position + 1] & BIT(bit_position));
}
