//
//  rmi_driver.h
//  VoodooSMBus
//
//  Created by Avery Black on 5/6/20.
//  Copyright © 2020 leo-labs. All rights reserved.
//

#ifndef rmi_driver_h
#define rmi_driver_h

#include <IOKit/IOBufferMemoryDescriptor.h>
#include "RMIBus.hpp"

#define PDT_PROPERTIES_LOCATION 0x00EF
#define BSR_LOCATION 0x00FE

#define RMI_PDT_PROPS_HAS_BSR 0x02

#define NAME_BUFFER_SIZE 256

#define RMI_PDT_ENTRY_SIZE 6
#define RMI_PDT_FUNCTION_VERSION_MASK   0x60
#define RMI_PDT_INT_SOURCE_COUNT_MASK   0x07

#define PDT_START_SCAN_LOCATION 0x00e9
#define PDT_END_SCAN_LOCATION    0x0005
#define RMI4_END_OF_PDT(id) ((id) == 0x00 || (id) == 0xff)

struct pdt_entry {
    u16 page_start;
    u8 query_base_addr;
    u8 command_base_addr;
    u8 control_base_addr;
    u8 data_base_addr;
    u8 interrupt_source_count;
    u8 function_version;
    u8 function_number;
};

int rmi_driver_probe(RMIBus *dev);
int rmi_initial_reset(RMIBus *dev, void *ctx, const struct pdt_entry *pdt);
int rmi_scan_pdt(RMIBus *dev, void *ctx,
                int (*callback)(RMIBus* dev,
                                void *ctx, const struct pdt_entry *entry));
int rmi_probe_interrupts(rmi_driver_data *data);
int rmi_init_functions(struct rmi_driver_data *data);
void rmi_free_function_list(RMIBus *rmi_dev);
int rmi_enable_sensor(RMIBus *rmi_dev);

static inline UInt64 OSBitwiseAtomic64(unsigned long and_mask, unsigned long or_mask, unsigned long xor_mask, unsigned long * value)
{
    unsigned long    oldValue;
    unsigned long    newValue;
    
    do {
        oldValue = *value;
        newValue = ((oldValue & and_mask) | or_mask) ^ xor_mask;
    } while (! OSCompareAndSwap64(oldValue, newValue, value));
    
    return oldValue;
}

static inline unsigned long OSBitAndAtomic64(unsigned long mask, unsigned long * value)
{
    return OSBitwiseAtomic64(mask, 0, 0, value);
}

static inline unsigned long OSBitOrAtomic64(unsigned long mask, unsigned long * value)
{
    return OSBitwiseAtomic64(-1, mask, 0, value);
}

// bits.h

#define BITS_PER_LONG       (__CHAR_BIT__ * __SIZEOF_LONG__)
#define BIT_MASK(nr)        ((1) << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)        ((nr) / BITS_PER_LONG)

// asm-generic/bitops/atomic.h

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 *
 * Note: there are no guarantees that this function will not be reordered
 * on non x86 architectures, so if you are writing portable code,
 * make sure not to rely on its reordering guarantees.
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void set_bit(int nr, volatile unsigned long *addr)
{
    unsigned long mask = static_cast<unsigned long>(1) << (nr % BITS_PER_LONG);
    unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
    OSBitOrAtomic64(mask, p);
}

// end atomic.h
#endif /* rmi_driver_h */
