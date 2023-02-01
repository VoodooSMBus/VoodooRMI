// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Avery Black
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_driver.c
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 * This driver provides the core support for a single RMI4-based device.
 *
 * The RMI4 specification can be found here (URL split for line length):
 *
 * http://www.synaptics.com/sites/default/files/
 *      511-000136-01-Rev-E-RMI4-Interfacing-Guide.pdf
 */

#include "rmi_driver.hpp"
#include "RMIFunction.hpp"

static int rmi_driver_process_config_requests(RMIBus *rmi_dev)
{
    RMIFunction *func;
    
    OSIterator* iter = rmi_dev->getClientIterator();
    while ((func = OSDynamicCast(RMIFunction, iter->getNextObject()))) {
        if (func && !func->start(rmi_dev))
            IOLogError("Could not start function %s", func->getName());

    }
    
    OSSafeReleaseNULL(iter);
    
    return 0;
}

int rmi_driver_clear_irq_bits(RMIBus *rmi_dev)
{
    rmi_driver_data *data = rmi_dev->data;
    
//    unsigned long mask = getMask(rmi_dev);
    
    int error = 0;
    IOLockLock(data->irq_mutex);
//    data->fn_irq_bits &= ~mask;
//    data->new_irq_mask = data->current_irq_mask & ~mask;
    
//    error = rmi_dev->blockWrite(data->f01_container->fd.control_base_addr + 1,
//                                reinterpret_cast<u8*>(&data->new_irq_mask), data->num_of_irq_regs);
    
    if (error < 0) {
        IOLogError("%s: Filaed to change enabled interrupts!", __func__);
        goto error_unlock;
    }
    
    data->current_irq_mask = data->new_irq_mask;
    
error_unlock:
    IOLockUnlock(data->irq_mutex);
    return error;
}

int rmi_driver_set_irq_bits(RMIBus *rmi_dev)
{
    int error = 0;
//    unsigned long mask = getMask(rmi_dev)
    rmi_driver_data *data = rmi_dev->data;
    
    IOLockLock(data->irq_mutex);
    // Dummy read in order to clear irqs
    error = rmi_dev->readBlock(data->f01_container->fd.data_base_addr + 1,
                               (u8 *)&data->irq_status, data->num_of_irq_regs);
    
    if (error < 0) {
        IOLogError("%s: Failed to read interrupt status!", __func__);
    }
    
//    data->new_irq_mask = mask | data->current_irq_mask;
    
    error = rmi_dev->blockWrite(data->f01_container->fd.control_base_addr + 1,
                                reinterpret_cast<u8*>(&data->new_irq_mask), data->num_of_irq_regs);
    if (error < 0) {
        IOLogError("%s: Failed to change enabled intterupts!", __func__);
        goto error_unlock;
    }
    
    data->current_irq_mask = data->new_irq_mask;
//    data->fn_irq_bits = mask | data->fn_irq_bits;
    
error_unlock:
    IOLockUnlock(data->irq_mutex);
    return error;
}

int rmi_enable_sensor(RMIBus *rmi_dev)
{
    int retval = 0;
    
    retval = rmi_driver_process_config_requests(rmi_dev);
    if (retval < 0)
        return retval;
    
    retval = rmi_driver_set_irq_bits(rmi_dev);
    if (retval < 0)
        return retval;

    return 0;
}

int rmi_init_functions(RMIBus *rmi_dev, rmi_driver_data *data)
{
    int irq_count = 0;
    int retval;
    
    IOLogDebug("%s: Creating functions", __func__);
//    retval = rmi_scan_pdt(rmi_dev, &irq_count, rmi_create_function);
    if (retval < 0) {
        IOLogError("Function creation failed with code %d", retval);
        return retval;
    }
    
    if (!data->f01_container) {
        IOLogError("Missing F01 container!");
        return -EINVAL;
    }
    
    retval = rmi_dev->readBlock(
                            data->f01_container->fd.control_base_addr + 1,
                            reinterpret_cast<u8 *>(&data->current_irq_mask), data->num_of_irq_regs);
    
    if (retval < 0) {
        IOLogError("%s: Failed to read current IRQ mask", __func__);
        return retval;
    }
    
    return 0;
}

void rmi_free_function_list(RMIBus *rmi_dev)
{
    struct rmi_driver_data *data = rmi_dev->data;
    
    IOLogDebug("Freeing function list");
    
    if (data->f01_container)
        IOFree(data->f01_container, data->f01_container->size);
    data->f01_container = NULL;
}

