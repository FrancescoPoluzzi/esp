// Copyright (c) 2011-2024 Columbia University, System Level Design Group
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include "xheep_firmware.h"
#include "core_v_mini_mcu.h"
#include "power_manager_regs.h"
#include "xheep_common.h"

#define XHEEP_BASE_ADDR 0x60400000u

int main(int argc, char **argv)
{
    printf("Hello from ESP!\n");
    
    volatile uint32_t *code = (volatile uint32_t *)(uintptr_t)(XHEEP_BASE_ADDR + RAM0_START_ADDRESS);
    const uint32_t *src = (const uint32_t *)xheep_firmware;
    unsigned words = (unsigned)(XHEEP_FIRMWARE_SIZE / 4u);

    for (unsigned i = 0; i < words; ++i) { code[i] = src[i]; }
    /* 2) Pulse software reset (addresses derived from X-HEEP headers) */
    *(volatile uint32_t *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_RESET_ASSERT_ADDR)   = 1u;
    *(volatile uint32_t *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_RESET_DEASSERT_ADDR) = 1u;
 
    /* 3) Read back the string from X-HEEP RAM and print it */
    volatile const char *p = (volatile const char *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_SHARED_STR_ADDR);
     char buf[XHEEP_SHARED_STR_MAX];
     unsigned i = 0;
     for (; i + 1 < XHEEP_SHARED_STR_MAX; ++i) {
         char c = p[i];
         buf[i] = c;
         if (c == '\0') break;
     }
     if (i + 1 >= XHEEP_SHARED_STR_MAX) buf[XHEEP_SHARED_STR_MAX - 1] = '\0';
 
     printf("X-HEEP says: %s\n", buf);
     return 0;
 }
 