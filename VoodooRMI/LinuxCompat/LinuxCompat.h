/* SPDX-License-Identifier: GPL-2.0-only
 * Macros and functions brought over from the linux kernel headers
 * https://github.com/torvalds/linux/tree/master/include
 */

#ifndef linux_compat_h
#define linux_compat_h

// https://github.com/torvalds/linux/blob/master/include/uapi/asm-generic/errno-base.h
#define EIO     5
#define ENOMEM  12
#define ENODEV  19
#define EINVAL  22

#define BITS_PER_LONG       (BITS_PER_BYTE * __SIZEOF_LONG__)
#define BIT(nr) (1UL << (nr))

// bitops.h
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

// bits.h
#define BITS_PER_BYTE           8

// kernel.h
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

// input.h
#define KEY_RESERVED        0
#define BTN_LEFT            1

// lib/bitmap.c
static inline void bitmap_set (unsigned long *bitmap, unsigned int start, unsigned int nbits)
{
    int bitmapIndex = start / BITS_PER_LONG;
    start %= BITS_PER_LONG;
    
    while (nbits > 0) {
        unsigned long mask = 1UL << start;
        bitmap[bitmapIndex] |= mask;
        
        start++;
        nbits--;
        
        if (start >= BITS_PER_LONG) {
            start = 0;
            bitmapIndex++;
        }
    }
}

static int ffsll(const unsigned long bitmap) {
    int i = 0;
    
    while (i < sizeof(unsigned long) * BITS_PER_BYTE) {
        if (bitmap & 1 << i) {
            break;
        }
        i++;
    }
    
    return i;
}

static inline int find_first_bit (const unsigned long *bitmap, int bits)
{
    int lim = bits/BITS_PER_LONG, res = 0;
    
    for (int i = 0; i < lim; i++) {
        res = ffsll(bitmap[i]);
        if (res != sizeof(unsigned long) * BITS_PER_LONG)
            return (i * BITS_PER_LONG) + res;
    }
    
    return res;
}

static inline int find_next_bit (const unsigned long *bitmap, int bits, int offset)
{
    int lim = bits/BITS_PER_LONG;
    int startLong = offset / BITS_PER_LONG;
    int startBit = offset % BITS_PER_LONG;
    
    for (int i = startLong; i < lim; i++) {
        for (int bit = startBit; bit < BITS_PER_LONG; bit++) {
            startBit = 0;
            if (bitmap[i] & 1UL << bit)
                return bit + (i * BITS_PER_LONG);
        }
    }
    
    return bits;
}

static int hweight_long(unsigned long value)
{
    int weight = 0;
    for (int i = 0; i < BITS_PER_LONG; i++)
        if (value & (1UL << i))
            weight++;
    
    return weight;
}

static inline int bitmap_weight(const unsigned long *bitmap, int bits)
{
    int k, w = 0, lim = bits/BITS_PER_LONG;
    
    for (k = 0; k < lim; k++)
        w += hweight_long(bitmap[k]);
    
    return w;
}

#endif /* linux_compat_h */
