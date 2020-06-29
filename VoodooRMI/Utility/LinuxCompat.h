/* SPDX-License-Identifier: GPL-2.0-only
 * Macros and functions brought over from the linux kernel headers
 * https://github.com/torvalds/linux/tree/master/include
 */

#ifndef linux_compat_h
#define linux_compat_h

#include <IOKit/IOLib.h>

typedef UInt8  u8;
typedef UInt16 u16;
typedef UInt32 u32;
typedef UInt64 u64;
typedef u8 __u8;
typedef u16 __u16;
typedef u32 __u32;
typedef u64 __u64;
typedef  SInt16 __be16;
typedef  SInt32 __be32;
typedef  SInt64 __be64;
typedef  SInt16 __le16;
typedef  SInt32 __le32;
typedef  SInt64 __le64;
typedef SInt8  s8;
typedef SInt16 s16;
typedef SInt32 s32;
typedef SInt64 s64;
typedef s8  __s8;
typedef s16 __s16;
typedef s32 __s32;
typedef s64 __s64;

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
#define BIT_MASK(nr)        ((1) << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)        ((nr) / BITS_PER_LONG)

// kernel.h
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

// unaligned/le_byteshift.h
static inline u32 __get_unaligned_le32(const u8 *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static inline u64 __get_unaligned_le64(const u8 *p)
{
    return (u64)__get_unaligned_le32(p + 4) << 32 |
    __get_unaligned_le32(p);
}

static inline u64 get_unaligned_le64(const void *p)
{
    return __get_unaligned_le64((const u8 *)p);
}

// input.h
#define KEY_RESERVED        0
#define BTN_LEFT            1

// bitmap.h
#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> (-(nbits) & (BITS_PER_LONG - 1)))

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

static inline int find_first_bit (const unsigned long *bitmap, int bits)
{
    int lim = bits/BITS_PER_LONG, res = 0;
    
    for (int i = 0; i < lim; i++) {
        res = ffsll(bitmap[i]);
        if (res)
            return (i * BITS_PER_LONG) + res - 1;
    }
    
    return res;
}

static inline int find_next_bit (const unsigned long *bitmap, int bits, int offset)
{
    int lim = bits/BITS_PER_LONG, start = offset/BITS_PER_LONG;
    
    offset %= BITS_PER_LONG;
    
    for (int i = start; i < lim; i++) {
        for (int bit = offset; bit < BITS_PER_LONG; bit++) {
            if (offset > 0) offset--;
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
