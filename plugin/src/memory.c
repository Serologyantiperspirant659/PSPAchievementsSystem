#include <string.h>
#include "memory.h"

/*
 * PSP Memory mapping for RetroAchievements:
 *
 * RA addresses are offsets from 0x08000000 (PSP RAM base).
 * ADD_ADDRESS conditions read game pointers (e.g. 0x09ABCDEF)
 * which are full PSP virtual addresses — we strip prefix with
 * a 25-bit mask (0x01FFFFFF) to keep the offset within 32MB.
 *
 * PSP physical RAM: 0x08000000–0x09FFFFFF (32MB)
 * kseg0 mapping:    0x88000000–0x89FFFFFF (cached, no TLB)
 *
 * The 25-bit mask maps ANY address (including full PSP pointers)
 * into the valid 32MB range. Worst case: garbage address wraps
 * into valid RAM and returns garbage data (no crash).
 */

#define KSEG0_BASE  0x88000000u
#define RAM_MASK    0x01FFFFFFu   /* 25-bit = 32MB */

static unsigned char safe_read8(unsigned int addr) {
    addr &= RAM_MASK;
    return *((volatile unsigned char *)(KSEG0_BASE + addr));
}

int pach_mem_valid(unsigned int ra_addr) {
    /* With masking, all addresses are technically readable.
       Check if address is in user partition (>= 8MB offset). */
    return ((ra_addr & RAM_MASK) >= 0x00800000u) ? 1 : 0;
}

unsigned char pach_mem_read8(unsigned int ra_addr) {
    return safe_read8(ra_addr);
}

unsigned short pach_mem_read16(unsigned int ra_addr) {
    ra_addr &= RAM_MASK;
    volatile unsigned char *p = (volatile unsigned char *)(KSEG0_BASE + ra_addr);
    return (unsigned short)p[0] | ((unsigned short)p[1] << 8);
}

unsigned int pach_mem_read24(unsigned int ra_addr) {
    ra_addr &= RAM_MASK;
    volatile unsigned char *p = (volatile unsigned char *)(KSEG0_BASE + ra_addr);
    return (unsigned int)p[0] |
           ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16);
}

unsigned int pach_mem_read32(unsigned int ra_addr) {
    ra_addr &= RAM_MASK;
    volatile unsigned char *p = (volatile unsigned char *)(KSEG0_BASE + ra_addr);
    return (unsigned int)p[0] |
           ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}

unsigned char pach_mem_read_bit0(unsigned int ra_addr) {
    return safe_read8(ra_addr) & 0x01;
}

float pach_mem_read_float_be(unsigned int ra_addr) {
    unsigned int raw = pach_mem_read32(ra_addr);
    float f;
    unsigned char *dst = (unsigned char *)&f;
    unsigned char *src = (unsigned char *)&raw;
    dst[0] = src[0]; dst[1] = src[1];
    dst[2] = src[2]; dst[3] = src[3];
    return f;
}