// Copyright (c) 2011-2024 Columbia University, System Level Design Group
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdint.h>
#include "xheep_firmware.h"
#include "core_v_mini_mcu.h"
#include "power_manager_regs.h"
#include "xheep_common.h"
#include <esp_accelerator.h>
#include "esp_probe.h"

#define XHEEP_0_BASE_ADDR 0x60400000u
#define MEMORY_BASE_ADDR 0x80000000u

#define TWO_XHEEP_INSTANCES 0

// PLIC (Platform-Level Interrupt Controller) definitions
#define PLIC_ADDR          0x6c000000
#define PLIC_IP_OFFSET     0x1000      // Interrupt pending register
#define PLIC_INTACK_OFFSET 0x200004    // Interrupt acknowledge register

// X-HEEP IRQ number (from socmap.vhd: xheep_0_pirq = 5)
#define XHEEP_IRQ 5

#if TWO_XHEEP_INSTANCES
    #define XHEEP_1_BASE_ADDR 0x60500000u
    // TODO: Define XHEEP_1_IRQ when second instance is configured
    #define XHEEP_1_IRQ 6  // Placeholder - verify actual IRQ assignment
    #include "xheep1_firmware.h"
#endif

int main(int argc, char **argv)
{
    printf("Hello from ESP!\n");
    printf("[DEBUG] Starting X-HEEP initialization...\n");

    // Setup PLIC device structure for interrupt polling
    struct esp_device plic_dev;
    plic_dev.addr = PLIC_ADDR;

    printf("Zeroing out buffers....\n");
    volatile char *q_8000 = (volatile char *)(uintptr_t)(MEMORY_BASE_ADDR + XHEEP_SHARED_STR_ADDR);
    char buf_axi[XHEEP_SHARED_STR_MAX];
    char buf[XHEEP_SHARED_STR_MAX];
    volatile unsigned *shared_mem = (volatile unsigned *)(uintptr_t)(XHEEP_0_BASE_ADDR + XHEEP_SHARED_STR_ADDR);
    /* Zero out external memory region and local AXI buffer */
    for (unsigned k = 0; k < XHEEP_SHARED_STR_MAX; ++k) {
        q_8000[k] = '\0';
        buf_axi[k] = '\0';
        buf[k] = '\0';
        if(k % 4 == 0) shared_mem[k / 4] = 0; // Zero out X-HEEP shared memory as well
    }

#if TWO_XHEEP_INSTANCES
    printf("[DEBUG] Initializing X-HEEP 1 shared memory at 0x%x to zero...\n", XHEEP_SHARED_STR_ADDR);
    volatile unsigned *shared_mem1 = (volatile unsigned *)(uintptr_t)(XHEEP_1_BASE_ADDR + XHEEP_SHARED_STR_ADDR);
    for (unsigned i = 0; i < XHEEP_SHARED_STR_MAX / 4; ++i) {
        shared_mem1[i] = 0;
    }
#endif

    /* 2) Load firmware sections to their respective addresses */
    printf("[DEBUG] Loading %u firmware sections to X-Heep 0...\n", XHEEP_FIRMWARE_NUM_SECTIONS);
    for (unsigned s = 0; s < XHEEP_FIRMWARE_NUM_SECTIONS; ++s) {
        const xheep_firmware_fw_section_t *section = &xheep_firmware_sections[s];
        volatile unsigned *dest = (volatile unsigned *)(uintptr_t)(XHEEP_0_BASE_ADDR + section->addr);
        const unsigned *src = section->data;
        unsigned words = (section->size + 3) / 4;  // Round up to word
        
        printf("[DEBUG] Section %u: 0x%08x, %u bytes (%u words)\n", 
               s, section->addr, section->size, words);
        
        for (unsigned i = 0; i < words; ++i) {
            dest[i] = src[i];
        }
    }
#if TWO_XHEEP_INSTANCES
    printf("[DEBUG] Loading %u firmware sections to X-Heep 1...\n", XHEEP1_FIRMWARE_NUM_SECTIONS);
    for (unsigned s = 0; s < XHEEP1_FIRMWARE_NUM_SECTIONS; ++s) {
        const xheep1_firmware_fw_section_t *section = &xheep1_firmware_sections[s];
        volatile unsigned *dest = (volatile unsigned *)(uintptr_t)(XHEEP_1_BASE_ADDR + section->addr);
        const unsigned *src = section->data;
        unsigned words = (section->size + 3) / 4;  // Round up to word
        
        printf("[DEBUG] Section %u: 0x%08x, %u bytes (%u words)\n", 
               s, section->addr, section->size, words);
        
        for (unsigned i = 0; i < words; ++i) {
            dest[i] = src[i];
        }
    }
#endif

    printf("[DEBUG] Firmware loaded successfully\n");

    /* 4) Program BOOT control (JTAG/debug path = 0; flash path = 1) */
    printf("[DEBUG] Setting BOOT_SELECT to JTAG/debug path (0) at address 0x%08x\n",
        (unsigned)(XHEEP_0_BASE_ADDR + XHEEP_BOOT_SELECT_ADDR));
    *(volatile unsigned *)(uintptr_t)(XHEEP_0_BASE_ADDR + XHEEP_BOOT_SELECT_ADDR) = 0u; // 0=JTAG/debug, 1=FLASH
    printf("[DEBUG] BOOT_SELECT set to %u\n", 
        *(volatile unsigned *)(uintptr_t)(XHEEP_0_BASE_ADDR + XHEEP_BOOT_SELECT_ADDR));

    /* 5) Set BOOT_ADDRESS register to firmware entry point */
    /* NOTE: This register is NOT reset by CPU reset, so set explicitly */
    printf("[DEBUG] Setting BOOT_ADDRESS register to 0x%x at address 0x%08x\n",
        XHEEP_FW_ENTRY_POINT, (unsigned)(XHEEP_0_BASE_ADDR + XHEEP_BOOT_ADDRESS_ADDR));
    *(volatile unsigned *)(uintptr_t)(XHEEP_0_BASE_ADDR + XHEEP_BOOT_ADDRESS_ADDR) = XHEEP_FW_ENTRY_POINT;
    printf("[DEBUG] BOOT_ADDRESS set to 0x%x\n",
        *(volatile unsigned *)(uintptr_t)(XHEEP_0_BASE_ADDR + XHEEP_BOOT_ADDRESS_ADDR));

    /* 7) Tell boot ROM to exit loop and jump to BOOT_ADDRESS (0x180) */
    printf("[DEBUG] Setting BOOT_EXIT_LOOP=1 at address 0x%08x\n",
        (unsigned)(XHEEP_0_BASE_ADDR + XHEEP_BOOT_EXIT_LOOP_ADDR));
    *(volatile unsigned *)(uintptr_t)(XHEEP_0_BASE_ADDR + XHEEP_BOOT_EXIT_LOOP_ADDR) = 1u;
    printf("[DEBUG] BOOT_EXIT_LOOP set to %u\n",
        *(volatile unsigned *)(uintptr_t)(XHEEP_0_BASE_ADDR + XHEEP_BOOT_EXIT_LOOP_ADDR));
    printf("[DEBUG] Boot ROM should now jump to firmware at 0x%03x (.__boot_address)\n", XHEEP_FW_ENTRY_POINT);

    // /* 9) Poll PLIC interrupt pending register for X-HEEP completion */
    // printf("[DEBUG] Polling PLIC for X-HEEP interrupt (IRQ %d)...\n", XHEEP_IRQ);
    // while ((ioread32(&plic_dev, PLIC_IP_OFFSET) & (1 << XHEEP_IRQ)) == 0) {
    //     // Busy-wait for X-HEEP to signal completion via interrupt
    // }
    // printf("[DEBUG] X-HEEP interrupt detected! Firmware execution complete.\n");

    /* 10) Read back the string from X-HEEP RAM and print it */
    printf("[DEBUG] Reading string from X-HEEP shared memory at 0x%08x...\n", 
            (unsigned)(XHEEP_0_BASE_ADDR + XHEEP_SHARED_STR_ADDR));
    volatile char *p = (volatile char *)(uintptr_t)(XHEEP_0_BASE_ADDR + XHEEP_SHARED_STR_ADDR);

    printf("[DEBUG] Polling for X-HEEP to write string...\n");
    const unsigned max_spin = 2u; 
    unsigned spins = 0;
    while (/*p[0] == '\0' &&*/ spins < max_spin) {
        spins++;
        /* Small delay between polls to give X-HEEP time */
        for (volatile unsigned d = 0; d < 100; d++);
    }
    // if (p[0] == '\0') {
    //     printf("[WARN] Shared string still empty after timeout\n");
    // }
    unsigned i = 0;
    printf("[DEBUG] Reading characters...\n");
    for (; i + 1 < XHEEP_SHARED_STR_MAX; ++i) {
        char c = p[i];
        buf[i] = c;
        if (c == '\0') break;
    }
    // if (i + 1 >= XHEEP_SHARED_STR_MAX) buf[XHEEP_SHARED_STR_MAX - 1] = '\0';

    printf("[DEBUG] String read complete (%u bytes)\n", i);
    printf("X-HEEP 0 says from APB: %s\n", buf);

    /* 11) Read back the string X-HEEP wrote to external memory via AXI */
    printf("[DEBUG] Reading string from external memory at 0x%08x...\n",
           (unsigned)MEMORY_BASE_ADDR);

    unsigned j = 0;
    for (j = 0 ; j + 1 < XHEEP_SHARED_STR_MAX; ++j) {
        char c = q_8000[j];
        buf_axi[j] = c;
        // if (c == '\0') break;
    }
    // if (j + 1 >= XHEEP_SHARED_STR_MAX) buf_axi[XHEEP_SHARED_STR_MAX - 1] = '\0';

    printf("[DEBUG] AXI string read complete (%u bytes)\n", j);
    printf("X-HEEP 0 says from AXI: %s\n", buf_axi);

    // /* 12) Acknowledge the interrupt in PLIC */
    // printf("[DEBUG] Acknowledging X-HEEP interrupt...\n");
    // iowrite32(&plic_dev, PLIC_INTACK_OFFSET, XHEEP_IRQ);

#if TWO_XHEEP_INSTANCES

    printf("Zeroing out buffers....\n");
    /* Zero out external memory region and local AXI buffer */
    for (unsigned k = 0; k < XHEEP_SHARED_STR_MAX; ++k) {
        q_8000[k] = '\0';
        buf_axi[k] = '\0';
        buf[k] = '\0';
    }

    printf("Now let's repeat the same operation with X-Heep 1.....");

    /* 1) Enable RAM banks via power manager for X-HEEP 1 */
    printf("[DEBUG] Enabling RAM banks in power manager for X-HEEP 1...\n");
    #define PM1_BASE (XHEEP_1_BASE_ADDR + POWER_MANAGER_START_ADDRESS)
    
    // Disable clock gating for RAM_0 and RAM_1
    *(volatile unsigned *)(uintptr_t)(PM1_BASE + POWER_MANAGER_RAM_0_CLK_GATE_REG_OFFSET) = 0x0;
    *(volatile unsigned *)(uintptr_t)(PM1_BASE + POWER_MANAGER_RAM_1_CLK_GATE_REG_OFFSET) = 0x0;
    printf("[DEBUG] RAM banks enabled for X-HEEP 1\n");

    /* 4) Program BOOT control (JTAG/debug path = 0; flash path = 1) */
    printf("[DEBUG] Setting BOOT_SELECT to JTAG/debug path (0) at address 0x%08x\n",
        (unsigned)(XHEEP_1_BASE_ADDR + XHEEP_BOOT_SELECT_ADDR));
    *(volatile unsigned *)(uintptr_t)(XHEEP_1_BASE_ADDR + XHEEP_BOOT_SELECT_ADDR) = 0u; // 0=JTAG/debug, 1=FLASH

    /* 5) Set BOOT_ADDRESS register to firmware entry point */
    /* NOTE: This register is NOT reset by CPU reset, so set explicitly */
    printf("[DEBUG] Setting BOOT_ADDRESS register to 0x%x at address 0x%08x\n",
        XHEEP_FW_ENTRY_POINT, (unsigned)(XHEEP_1_BASE_ADDR + XHEEP_BOOT_ADDRESS_ADDR));
    *(volatile unsigned *)(uintptr_t)(XHEEP_1_BASE_ADDR + XHEEP_BOOT_ADDRESS_ADDR) = XHEEP_FW_ENTRY_POINT;

    /* 7) Tell boot ROM to exit loop and jump to BOOT_ADDRESS (0x180) */
    printf("[DEBUG] Setting BOOT_EXIT_LOOP=1 at address 0x%08x\n",
        (unsigned)(XHEEP_1_BASE_ADDR + XHEEP_BOOT_EXIT_LOOP_ADDR));
    *(volatile unsigned *)(uintptr_t)(XHEEP_1_BASE_ADDR + XHEEP_BOOT_EXIT_LOOP_ADDR) = 1u;
    printf("[DEBUG] Boot ROM should now jump to firmware at 0x%03x (.__boot_address)\n", XHEEP_FW_ENTRY_POINT);

    /* 9) Poll PLIC interrupt pending register for X-HEEP 1 completion */
    printf("[DEBUG] Polling PLIC for X-HEEP 1 interrupt (IRQ %d)...\n", XHEEP_1_IRQ);
    while ((ioread32(&plic_dev, PLIC_IP_OFFSET) & (1 << XHEEP_1_IRQ)) == 0) {
        // Busy-wait for X-HEEP 1 to signal completion via interrupt
    }
    printf("[DEBUG] X-HEEP 1 interrupt detected! Firmware execution complete.\n");

    /* 10) Read back the string from X-HEEP RAM and print it */
    printf("[DEBUG] Reading string from X-HEEP shared memory at 0x%08x...\n", 
            (unsigned)(XHEEP_1_BASE_ADDR + XHEEP_SHARED_STR_ADDR));
    volatile const char *p_1 = (volatile const char *)(uintptr_t)(XHEEP_1_BASE_ADDR + XHEEP_SHARED_STR_ADDR);

    /* Poll until we get a non-zero first character (indicating X-HEEP wrote something) */
    spins = 0;
    while (p_1[0] == '\0' && spins < max_spin) {
        spins++;
        /* Small delay between polls to give X-HEEP time */
        for (volatile unsigned d = 0; d < 100; d++);
    }
    if (p_1[0] == '\0') {
        printf("[WARN] Shared string still empty after timeout\n");
    }

    i = 0;
    for (; i + 1 < XHEEP_SHARED_STR_MAX; ++i) {
        char c = p_1[i];
        buf[i] = c;
        if (c == '\0') break;
    }
    if (i + 1 >= XHEEP_SHARED_STR_MAX) buf[XHEEP_SHARED_STR_MAX - 1] = '\0';

    printf("[DEBUG] String read complete (%u bytes)\n", i);
    printf("X-HEEP 1 says from APB: %s\n", buf);

    /* 11) Read back the string X-HEEP wrote to external memory via AXI */
    printf("[DEBUG] Reading string from external memory at 0x%08x...\n",
           (unsigned)MEMORY_BASE_ADDR);

    for (j = 0 ; j + 1 < XHEEP_SHARED_STR_MAX; ++j) {
        char c = q_8000[j];
        buf_axi[j] = c;
        if (c == '\0') break;
    }
    if (j + 1 >= XHEEP_SHARED_STR_MAX) buf_axi[XHEEP_SHARED_STR_MAX - 1] = '\0';

    printf("[DEBUG] AXI string read complete (%u bytes)\n", j);
    printf("X-HEEP 1 says from AXI: %s\n", buf_axi);

    /* 12) Acknowledge the interrupt in PLIC for X-HEEP 1 */
    printf("[DEBUG] Acknowledging X-HEEP 1 interrupt...\n");
    iowrite32(&plic_dev, PLIC_INTACK_OFFSET, XHEEP_1_IRQ);

#endif

    return 0;

}
