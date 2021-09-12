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

#define HAS_NONSTANDARD_PDT_MASK 0x40
#define RMI4_MAX_PAGE 0xff
#define RMI4_PAGE_SIZE 0x100
#define RMI4_PAGE_MASK 0xFF00

#define RMI_DEVICE_RESET_CMD    0x01
#define DEFAULT_RESET_DELAY_MS    100

int rmi_driver_probe(RMIBus *dev)
{
    int retval;

    /*
     * Right before a warm boot, the sensor might be in some unusual state,
     * such as F54 diagnostics, or F34 bootloader mode after a firmware
     * or configuration update.  In order to clear the sensor to a known
     * state and/or apply any updates, we issue a initial reset to clear any
     * previous settings and force it into normal operation.
     *
     * We have to do this before actually building the PDT because
     * the reflash updates (if any) might cause various registers to move
     * around.
     *
     * For a number of reasons, this initial reset may fail to return
     * within the specified time, but we'll still be able to bring up the
     * driver normally after that failure.  This occurs most commonly in
     * a cold boot situation (where then firmware takes longer to come up
     * than from a warm boot) and the reset_delay_ms in the platform data
     * has been set too short to accommodate that.  Since the sensor will
     * eventually come up and be usable, we don't want to just fail here
     * and leave the customer's device unusable.  So we warn them, and
     * continue processing.
     */
    
    retval = rmi_scan_pdt(dev, NULL, rmi_initial_reset);
    if (retval < 0)
        IOLogError("RMI initial reset failed! Continuing in spite of this");
    
    retval = dev->read(PDT_PROPERTIES_LOCATION, &dev->data->pdt_props);
    if (retval < 0) {
        /*
         * we'll print out a warning and continue since
         * failure to get the PDT properties is not a cause to fail
         */
        IOLogError("Could not read PDT properties from %#06x (code %d). Assuming 0x00.",
                 PDT_PROPERTIES_LOCATION, retval);
    }
    
    retval = rmi_probe_interrupts(dev, dev->data);
    if (retval)
        goto err;
    
    return 0;
err:
    IOLogError("Could not probe");
    return retval;
}

static int rmi_read_pdt_entry(RMIBus *rmi_dev,
                              struct pdt_entry *entry, u16 pdt_address)
{
    u8 buf[RMI_PDT_ENTRY_SIZE];
    int error;
    
    error = rmi_dev->readBlock(pdt_address, buf, RMI_PDT_ENTRY_SIZE);
    if (error) {
        IOLogError("Read PDT entry at %#06x failed, code: %d", pdt_address, error);
        return error;
    }
    
    entry->page_start = pdt_address & RMI4_PAGE_MASK;
    entry->query_base_addr = buf[0];
    entry->command_base_addr = buf[1];
    entry->control_base_addr = buf[2];
    entry->data_base_addr = buf[3];
    entry->interrupt_source_count = buf[4] & RMI_PDT_INT_SOURCE_COUNT_MASK;
    entry->function_version = (buf[4] & RMI_PDT_FUNCTION_VERSION_MASK) >> 5;
    entry->function_number = buf[5];
    
    return 0;
}

static int rmi_scan_pdt_page(RMIBus *dev,
                             int page,
                             int *empty_pages,
                             void *ctx,
                             int (*callback)(RMIBus *rmi_dev,
                                             void *ctx,
                                             const struct pdt_entry *entry))
{
    rmi_driver_data *data = dev->data;
    struct pdt_entry pdt_entry;
    u16 page_start = RMI4_PAGE_SIZE * page;
    u16 pdt_start = page_start + PDT_START_SCAN_LOCATION;
    u16 pdt_end = page_start + PDT_END_SCAN_LOCATION;
    u16 addr;
    int error;
    int retval;
    
    for (addr = pdt_start; addr >= pdt_end; addr -= RMI_PDT_ENTRY_SIZE) {
        error = rmi_read_pdt_entry(dev, &pdt_entry, addr);
        if (error)
            return error;
        
        if (RMI4_END_OF_PDT(pdt_entry.function_number))
            break;
        
        retval = callback(dev, ctx, &pdt_entry);
        if (retval != RMI_SCAN_CONTINUE)
            return retval;
    }
    
    /*
     * Count number of empty PDT pages. If a gap of two pages
     * or more is found, stop scanning.
     */
    if (addr == pdt_start)
        ++*empty_pages;
    else
        *empty_pages = 0;
    
    return (data->bootloader_mode || *empty_pages >= 2) ?
        RMI_SCAN_DONE : RMI_SCAN_CONTINUE;
}

int rmi_scan_pdt(RMIBus *dev, void *ctx,
                 int (*callback)(RMIBus* dev,
                                 void *ctx, const struct pdt_entry *entry))
{
    int page;
    int empty_pages = 0;
    int retval = RMI_SCAN_DONE;
    
    for (page = 0; page <= RMI4_MAX_PAGE; page++) {
        retval = rmi_scan_pdt_page(dev, page, &empty_pages,
                                   ctx, callback);
        if (retval != RMI_SCAN_CONTINUE)
            break;
    }
    
    return retval < 0 ? retval : 0;
}

int rmi_initial_reset(RMIBus *dev, void *ctx, const struct pdt_entry *pdt)
{
    int error;
    
    if (pdt->function_number == 0x01) {
        error = dev->reset();
        if (error < 0) {
            IOLogError("Unable to reset");
            return error;
        }

        return RMI_SCAN_DONE;
    }
    
    /* F01 should always be on page 0. If we don't find it there, fail. */
    return pdt->page_start == 0 ? RMI_SCAN_CONTINUE : -ENODEV;
}

static int rmi_check_bootloader_mode(RMIBus *rmi_dev,
                                     const struct pdt_entry *pdt)
{
    struct rmi_driver_data *data = rmi_dev->data;
    int ret;
    u8 status;
    
    if (pdt->function_number == 0x34 && pdt->function_version > 1) {
        ret = rmi_dev->read(pdt->data_base_addr, &status);
        if (ret) {
            IOLogError("Failed to read F34 status: %d", ret);
            return ret;
        }
        
        if (status & BIT(7))
            data->bootloader_mode = true;
    } else if (pdt->function_number == 0x01) {
        ret = rmi_dev->read(pdt->data_base_addr, &status);
        if (ret) {
            IOLogError("Failed to read F01 status: %d", ret);
            return ret;
        }
        
        if (status & BIT(6))
            data->bootloader_mode = true;
    }
    
    return 0;
}

static int rmi_count_irqs(RMIBus *rmi_dev,
                          void *ctx, const struct pdt_entry *pdt)
{
    int *irq_count = reinterpret_cast<int *>(ctx);
    int ret;
    
    *irq_count += pdt->interrupt_source_count;
    
    ret = rmi_check_bootloader_mode(rmi_dev, pdt);
    if (ret < 0)
        return ret;
    
    return RMI_SCAN_CONTINUE;
}

int rmi_probe_interrupts(RMIBus *rmi_dev, rmi_driver_data *data)
{
    int irq_count = 0;
    int retval;
    
    /*
     * We need to count the IRQs and allocate their storage before scanning
     * the PDT and creating the function entries, because adding a new
     * function can trigger events that result in the IRQ related storage
     * being accessed.
     */
    IOLogDebug("%s: Counting IRQs", __func__);
    data->bootloader_mode = false;
    
    retval = rmi_scan_pdt(rmi_dev, &irq_count, rmi_count_irqs);
    if (retval < 0) {
        IOLogError("IRQ counting failed with code %d", retval);
        return retval;
    }
    
    if (data->bootloader_mode)
        IOLogDebug("Device in bootloader mode");
    
    data->irq_count = irq_count;
    data->num_of_irq_regs = (data->irq_count + 7) / 8;
    IOLogDebug("IRQ Count: %d", data->irq_count);
    
    data->irq_status        = 0;
    data->fn_irq_bits       = 0;
    data->current_irq_mask  = 0;
    data->new_irq_mask      = 0;
    
    return retval;
}

static void rmi_driver_copy_pdt_to_fd(const struct pdt_entry *pdt,
                                      struct rmi_function_descriptor *fd)
{
    fd->query_base_addr = pdt->query_base_addr + pdt->page_start;
    fd->command_base_addr = pdt->command_base_addr + pdt->page_start;
    fd->control_base_addr = pdt->control_base_addr + pdt->page_start;
    fd->data_base_addr = pdt->data_base_addr + pdt->page_start;
    fd->function_number = pdt->function_number;
    fd->interrupt_source_count = pdt->interrupt_source_count;
    fd->function_version = pdt->function_version;
}

static int rmi_create_function(RMIBus *rmi_dev,
                               void *ctx, const struct pdt_entry *pdt)
{
    struct rmi_driver_data *data = rmi_dev->data;
    int *current_irq_count = reinterpret_cast<int *>(ctx);
    struct rmi_function *fn;
    int i;
    int error;
    
    IOLogInfo("Initializing F%02X.", pdt->function_number);
    
    int size = sizeof(rmi_function)
        + BITS_TO_LONGS(data->irq_count) * sizeof(unsigned long);
    
    fn = reinterpret_cast<rmi_function*>(IOMalloc(size));
    memset (fn, 0, size);
    
    fn->size = size;
    
    if (!fn) {
        IOLogError("Failed to allocate memory for F%02X", pdt->function_number);
        return -ENOMEM;
    }
    
    rmi_driver_copy_pdt_to_fd(pdt, &fn->fd);
    
    fn->num_of_irqs = pdt->interrupt_source_count;
    fn->irq_pos = *current_irq_count;
    *current_irq_count += fn->num_of_irqs;
    
    for (i = 0; i < fn->num_of_irqs; i++)
        set_bit(fn->irq_pos + i, fn->irq_mask);
    
    error = rmi_dev->rmi_register_function(fn);
    if (error)
        return error;
    
    // Keep F01 around for reading/writing IRQ
    if (pdt->function_number == 0x01)
        data->f01_container = fn;
    else IOFree(fn, size);
    
    return RMI_SCAN_CONTINUE;
}

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

/*
 * This isn't from rmi_driver
 * Just getting the mask from all the started functions
 */
static unsigned long getMask(RMIBus *rmiBus)
{
    RMIFunction *func;
    unsigned long mask = 0;
    
    OSIterator* iter = rmiBus->getClientIterator();
    while ((func = OSDynamicCast(RMIFunction, iter->getNextObject()))) {
        unsigned long funcMask = func->getIRQ();
        mask |= funcMask;
    }
    
    OSSafeReleaseNULL(iter);
    
    return mask;
}

int rmi_driver_clear_irq_bits(RMIBus *rmi_dev)
{
    rmi_driver_data *data = rmi_dev->data;
    
    unsigned long mask = getMask(rmi_dev);
    
    int error = 0;
    IOLockLock(data->irq_mutex);
    data->fn_irq_bits &= ~mask;
    data->new_irq_mask = data->current_irq_mask & ~mask;
    
    error = rmi_dev->blockWrite(data->f01_container->fd.control_base_addr + 1,
                                reinterpret_cast<u8*>(&data->new_irq_mask), data->num_of_irq_regs);
    
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
    unsigned long mask = getMask(rmi_dev);
    rmi_driver_data *data = rmi_dev->data;
    
    IOLockLock(data->irq_mutex);
    // Dummy read in order to clear irqs
    error = rmi_dev->readBlock(data->f01_container->fd.data_base_addr + 1,
                               (u8 *)&data->irq_status, data->num_of_irq_regs);
    
    if (error < 0) {
        IOLogError("%s: Failed to read interrupt status!", __func__);
    }
    
    data->new_irq_mask = mask | data->current_irq_mask;
    
    error = rmi_dev->blockWrite(data->f01_container->fd.control_base_addr + 1,
                                reinterpret_cast<u8*>(&data->new_irq_mask), data->num_of_irq_regs);
    if (error < 0) {
        IOLogError("%s: Failed to change enabled intterupts!", __func__);
        goto error_unlock;
    }
    
    data->current_irq_mask = data->new_irq_mask;
    data->fn_irq_bits = mask | data->fn_irq_bits;
    
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
    retval = rmi_scan_pdt(rmi_dev, &irq_count, rmi_create_function);
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

