// Copyright (c) 2011-2024 Columbia University, System Level Design Group
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdint.h>
#include "xheep_firmware.h"
#include "core_v_mini_mcu.h"
#include "power_manager_regs.h"
#include "xheep_common.h"

#define XHEEP_BASE_ADDR 0x60400000u
#define MEMORY_BASE_ADDR 0x80000000u

int main(int argc, char **argv)
{
    printf("Hello from ESP!\n");
    printf("[DEBUG] Starting X-HEEP initialization...\n");

    // /* 1) Ensure CPU is disabled (held in reset) before loading firmware */
    // printf("[DEBUG] Disabling X-HEEP CPU (keeping in reset)...\n");
    // *(volatile unsigned *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_CPU_ENABLE_ADDR) = 0u;

    // /* 2.5) Trigger external reset to X-HEEP to ensure clean state */
    // printf("[DEBUG] Triggering external reset...\n", XHEEP_SHARED_STR_ADDR);
    // volatile unsigned *external_reset = (volatile unsigned *)(uintptr_t)(XHEEP_BASE_ADDR + 0x100u);
    // external_reset[0] = 0u;


    /* 2.5) Initialize shared memory location to prevent X propagation */
    printf("[DEBUG] Initializing shared memory at 0x%x to zero...\n", XHEEP_SHARED_STR_ADDR);
    volatile unsigned *shared_mem = (volatile unsigned *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_SHARED_STR_ADDR);
    for (unsigned i = 0; i < XHEEP_SHARED_STR_MAX / 4; ++i) {
        shared_mem[i] = 0;
    }

    /* 2) Load firmware sections to their respective addresses */
    printf("[DEBUG] Loading %u firmware sections...\n", XHEEP_FIRMWARE_NUM_SECTIONS);
    
    for (unsigned s = 0; s < XHEEP_FIRMWARE_NUM_SECTIONS; ++s) {
        const fw_section_t *section = &xheep_firmware_sections[s];
        volatile unsigned *dest = (volatile unsigned *)(uintptr_t)(XHEEP_BASE_ADDR + section->addr);
        const unsigned *src = section->data;
        unsigned words = (section->size + 3) / 4;  // Round up to word
        
        printf("[DEBUG] Section %u: 0x%08x, %u bytes (%u words)\n", 
               s, section->addr, section->size, words);
        
        for (unsigned i = 0; i < words; ++i) {
            dest[i] = src[i];
        }
    }

    printf("[DEBUG] Firmware loaded successfully\n");

    // /* 2.8) Make sure CPU domain is ON and RAM clocks are ungated (mimics TB power/clock state) */
    // {
    //  volatile unsigned *pm_base = (volatile unsigned *)(uintptr_t)(XHEEP_BASE_ADDR);
    //  /* Force CPU domain SWITCH ON */
    //  printf("[DEBUG] Forcing CPU domain switch ON... (PM@0x%08x + 0x%02x)\n",
    //      (unsigned)(XHEEP_BASE_ADDR + XHEEP_POWER_MANAGER_WRITE_OFFSET), (unsigned)XHEEP_MASTER_CPU_FORCE_SWITCH_ON_REG_OFFSET);
    //  pm_base[XHEEP_MASTER_CPU_FORCE_SWITCH_ON_REG_OFFSET >> 2] = 1u;
    //  /* Ungate RAM clocks (in case they default gated) */
    //  printf("[DEBUG] Ungating RAM_0/RAM_1 clocks...\n");
    //  pm_base[XHEEP_RAM_0_CLK_GATE_REG_OFFSET >> 2] = 0u;
    //  pm_base[XHEEP_RAM_1_CLK_GATE_REG_OFFSET >> 2] = 0u;
    // }

    // /* 3) Put CPU in reset while we program boot regs */
    // printf("[DEBUG] Asserting X-HEEP CPU reset... at address 0x%08x\n",
    //     (unsigned)(XHEEP_BASE_ADDR + XHEEP_RESET_ASSERT_ADDR));
    // *(volatile unsigned *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_RESET_ASSERT_ADDR) = 1u;

    /* 4) Program BOOT control (JTAG/debug path = 0; flash path = 1) */
    printf("[DEBUG] Setting BOOT_SELECT to JTAG/debug path (0) at address 0x%08x\n",
        (unsigned)(XHEEP_BASE_ADDR + XHEEP_BOOT_SELECT_ADDR));
    *(volatile unsigned *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_BOOT_SELECT_ADDR) = 0u; // 0=JTAG/debug, 1=FLASH

    /* 5) Set BOOT_ADDRESS register to firmware entry point */
    /* NOTE: This register is NOT reset by CPU reset, so set explicitly */
    printf("[DEBUG] Setting BOOT_ADDRESS register to 0x%x at address 0x%08x\n",
        XHEEP_FW_ENTRY_POINT, (unsigned)(XHEEP_BASE_ADDR + XHEEP_BOOT_ADDRESS_ADDR));
    *(volatile unsigned *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_BOOT_ADDRESS_ADDR) = XHEEP_FW_ENTRY_POINT;

    // /* 6) Release CPU reset so boot ROM starts executing */
    // printf("[DEBUG] Deasserting X-HEEP CPU reset... at address 0x%08x\n",
    //     (unsigned)(XHEEP_BASE_ADDR + XHEEP_RESET_DEASSERT_ADDR));
    // *(volatile unsigned *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_RESET_DEASSERT_ADDR) = 1u;

    /* 7) Tell boot ROM to exit loop and jump to BOOT_ADDRESS (0x180) */
    printf("[DEBUG] Setting BOOT_EXIT_LOOP=1 at address 0x%08x\n",
        (unsigned)(XHEEP_BASE_ADDR + XHEEP_BOOT_EXIT_LOOP_ADDR));
    *(volatile unsigned *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_BOOT_EXIT_LOOP_ADDR) = 1u;
    printf("[DEBUG] Boot ROM should now jump to firmware at 0x%03x (.__boot_address)\n", XHEEP_FW_ENTRY_POINT);

    /* 9) Add delay to let X-HEEP firmware execute before polling */
    printf("[DEBUG] Waiting for X-HEEP to execute...\n");
    for (volatile unsigned delay = 0; delay < 100; delay++) {
        /* busy wait to give X-HEEP time to run */
    }

    /* 10) Read back the string from X-HEEP RAM and print it */
    printf("[DEBUG] Reading string from X-HEEP shared memory at 0x%08x...\n", 
            (unsigned)(XHEEP_BASE_ADDR + XHEEP_SHARED_STR_ADDR));
    volatile const char *p = (volatile const char *)(uintptr_t)(XHEEP_BASE_ADDR + XHEEP_SHARED_STR_ADDR);

    /* Poll until we get a non-zero first character (indicating X-HEEP wrote something) */
    const unsigned max_spin = 2u; 
    unsigned spins = 0;
    while (p[0] == '\0' && spins < max_spin) {
        spins++;
        /* Small delay between polls to give X-HEEP time */
        for (volatile unsigned d = 0; d < 100; d++);
    }
    if (p[0] == '\0') {
        printf("[WARN] Shared string still empty after timeout\n");
    }

    char buf[XHEEP_SHARED_STR_MAX];
    unsigned i = 0;
    for (; i + 1 < XHEEP_SHARED_STR_MAX; ++i) {
        char c = p[i];
        buf[i] = c;
        if (c == '\0') break;
    }
    if (i + 1 >= XHEEP_SHARED_STR_MAX) buf[XHEEP_SHARED_STR_MAX - 1] = '\0';

    printf("[DEBUG] String read complete (%u bytes)\n", i);
    printf("X-HEEP says from APB: %s\n", buf);

    // printf("[DEBUG] Waiting for X-HEEP to execute...\n");
    // for (volatile unsigned delay = 0; delay < 200; delay++) {
    //     /* busy wait to give X-HEEP time to run */
    // }

    /* 11) Read back the string X-HEEP wrote to external memory via AXI */
    printf("[DEBUG] Reading string from external memory at 0x%08x...\n",
           (unsigned)MEMORY_BASE_ADDR);
    volatile const char *q_8000 = (volatile const char *)(uintptr_t)(MEMORY_BASE_ADDR + XHEEP_SHARED_STR_ADDR);

    char buf_axi[XHEEP_SHARED_STR_MAX];
    unsigned j = 0;
    for (j = 0 ; j + 1 < XHEEP_SHARED_STR_MAX; ++j) {
        char c = q_8000[j];
        buf_axi[j] = c;
        if (c == '\0') break;
    }
    if (j + 1 >= XHEEP_SHARED_STR_MAX) buf_axi[XHEEP_SHARED_STR_MAX - 1] = '\0';

    printf("[DEBUG] AXI string read complete (%u bytes)\n", j);
    printf("X-HEEP says from AXI: %s\n", buf_axi);

    // /* 11) Read back the string X-HEEP wrote to external memory via AXI */
    // printf("[DEBUG] Reading string from external memory at 0x%08x...\n",
    //        (unsigned)0u);
    // volatile const char *q_0 = (volatile const char *)(uintptr_t)(0u + XHEEP_SHARED_STR_ADDR);

    // for (j = 0 ; j + 1 < XHEEP_SHARED_STR_MAX; ++j) {
    //     char c = q_0[j];
    //     buf_axi[j] = c;
    //     if (c == '\0') break;
    // }
    // if (j + 1 >= XHEEP_SHARED_STR_MAX) buf_axi[XHEEP_SHARED_STR_MAX - 1] = '\0';

    // printf("[DEBUG] AXI string read complete (%u bytes)\n", j);
    // printf("X-HEEP says from AXI: %s\n", buf_axi);

    // /* 11) Read back the string X-HEEP wrote to external memory via AXI */
    // printf("[DEBUG] Reading string from external memory at 0x%08x...\n",
    //        (unsigned)(0xa0200000 + XHEEP_SHARED_STR_ADDR));
    // volatile const char *q_thirdparty_reserved = (volatile const char *)(uintptr_t)(0xa0200000 + XHEEP_SHARED_STR_ADDR);

    // for (j = 0 ; j + 1 < XHEEP_SHARED_STR_MAX; ++j) {
    //     char c = q_thirdparty_reserved[j];
    //     buf_axi[j] = c;
    //     if (c == '\0') break;
    // }
    // if (j + 1 >= XHEEP_SHARED_STR_MAX) buf_axi[XHEEP_SHARED_STR_MAX - 1] = '\0';

    // printf("[DEBUG] AXI string read complete (%u bytes)\n", j);
    // printf("X-HEEP says from AXI: %s\n", buf_axi);

}
