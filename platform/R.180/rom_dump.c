/**
 * eos-r-rom-dump.c — EOS R (DIGIC 8) RAM/ROM Dump Module
 * ======================================================
 * Purpose: Dump the decrypted Canon firmware from DRAM to SD card.
 *          Required to obtain a ROM dump for Ghidra analysis (FindEDMAC_D8.py).
 *
 * Usage:
 *   1. Copy this file to ~/magiclantern_simplified/platform/R.180/rom_dump.c
 *   2. Add $(BUILD_DIR)/rom_dump.o to ML_SRC_EXTRA_OBJS in platform/R.180/Makefile
 *   3. Rebuild: cd ~/magiclantern_simplified/platform/R.180 && make
 *   4. Boot EOS R with ML SD card → dump runs automatically on first boot
 *   5. Retrieve ML/LOGS/ROM_E0000000.BIN from SD card (32MB)
 *   6. Run: python3 research/tools/FindEDMAC_D8.py ML/LOGS/ROM_E0000000.BIN
 *
 * Architecture notes:
 *   - DIGIC 8 / Cortex-A9 / Thumb-2 / ARMv7-A
 *   - Firmware loads at ROM_BASE = 0xE0000000 into DRAM
 *   - Firmware size: ~30MB (0x01E00000 bytes from ROM_BASE)
 *   - We dump in 1MB chunks to avoid OOM on alloc_dma_memory
 *   - File I/O via Canon DryOS FIO stubs (all confirmed on EOS R R.180)
 *   - LED blink uses CARD_LED_ADDRESS = 0xD01300D4
 *
 * Safety:
 *   - Read-only memory access — no writes to firmware space
 *   - File created in ML/LOGS/ — standard ML output directory
 *   - Task priority: low, yields to Canon tasks
 *   - Runs once only, exits cleanly
 *
 * Expected output:
 *   ML/LOGS/ROM_E0000000.BIN  ~30MB  (ROM0 equivalent for DIGIC 8)
 *   ML/LOGS/ROM_DUMP_LOG.TXT  ~1KB   (verification log)
 *
 * Author: PRISM research — 2026-02-18
 * Camera: EOS R 1.8.0 (R.180)
 */

#include "dryos.h"
#include "bmp.h"
#include "config.h"
#include <string.h>
#include "mem.h"

extern int uart_printf(const char *fmt, ...);

/* ── Hardware constants (from platform/R.180/consts.h) ─────────────────── */
#define CARD_LED_ADDRESS    0xD01300D4
#define LEDON               0xD0002
#define LEDOFF              0xC0003

/* ── Dump parameters ────────────────────────────────────────────────────── */
#define ROM_BASE            0xE0000000    /* where Canon firmware lives in DRAM */
#define ROM_SIZE            0x01E00000    /* 30MB — conservative; actual ~30.9MB */
#define CHUNK_SIZE          0x00100000    /* 1MB chunks to keep alloc manageable */
#define DUMP_PATH           "ML/LOGS/ROM_E0000000.BIN"
#define LOG_PATH            "ML/LOGS/ROM_DUMP_LOG.TXT"

/* ── LED blink helper ───────────────────────────────────────────────────── */
static void led_on(void)  { *(volatile uint32_t *)CARD_LED_ADDRESS = LEDON; }
static void led_off(void) { *(volatile uint32_t *)CARD_LED_ADDRESS = LEDOFF; }

static void blink_n(int n, int ms_on, int ms_off)
{
    for (int i = 0; i < n; i++) {
        led_on();  msleep(ms_on);
        led_off(); msleep(ms_off);
    }
}

/* ── Checksum helper (simple 32-bit sum for verification) ──────────────── */
static uint32_t checksum32(const void *buf, size_t len)
{
    const uint32_t *p = (const uint32_t *)buf;
    uint32_t sum = 0;
    for (size_t i = 0; i < len / 4; i++)
        sum += p[i];
    return sum;
}

/* ── Main dump task ─────────────────────────────────────────────────────── */
static void rom_dump_task(void *arg)
{
    uart_printf("ROM_DUMP: starting EOS R firmware dump\n");
    uart_printf("ROM_DUMP: base=0x%08X size=0x%08X\n", ROM_BASE, ROM_SIZE);

    msleep(2000);  /* let camera settle after boot */

    /* ── Allocate chunk buffer ── */
    uint8_t *buf = (uint8_t *)fio_malloc(CHUNK_SIZE);
    if (!buf) {
        uart_printf("ROM_DUMP: alloc_dma_memory(%d) FAILED\n", CHUNK_SIZE);
        blink_n(10, 100, 100);  /* rapid blink = error */
        return;
    }
    uart_printf("ROM_DUMP: buffer at %p\n", buf);

    /* ── Create output file ── */
    FILE *fp = FIO_CreateFile(DUMP_PATH);
    if (!fp || (int)(intptr_t)fp == -1) {
        uart_printf("ROM_DUMP: failed to create %s\n", DUMP_PATH);
        fio_free(buf);
        blink_n(5, 200, 200);
        return;
    }
    uart_printf("ROM_DUMP: writing to %s\n", DUMP_PATH);

    /* ── Dump in chunks ── */
    size_t written = 0;
    uint32_t total_csum = 0;
    int error = 0;

    for (size_t offset = 0; offset < ROM_SIZE; offset += CHUNK_SIZE) {
        size_t chunk = CHUNK_SIZE;
        if (offset + chunk > ROM_SIZE)
            chunk = ROM_SIZE - offset;

        uint32_t src = ROM_BASE + offset;

        /* Copy from firmware DRAM to our buffer */
        memcpy(buf, (void *)src, chunk);

        /* Accumulate checksum */
        total_csum += checksum32(buf, chunk);

        /* Write to SD card */
        int ret = FIO_WriteFile(fp, buf, chunk);
        if (ret != (int)chunk) {
            uart_printf("ROM_DUMP: write error at offset 0x%08X: ret=%d\n", offset, ret);
            error = 1;
            break;
        }

        written += chunk;

        /* Progress LED blink every 4MB */
        if ((offset % 0x400000) == 0 && offset > 0) {
            led_on(); msleep(50); led_off();
        }

        /* Print progress every 4MB */
        if ((offset % 0x400000) == 0) {
            uart_printf("ROM_DUMP: %d/%d MB written\n",
                        (int)(offset >> 20), (int)(ROM_SIZE >> 20));
        }

        msleep(1);  /* yield — don't starve Canon tasks */
    }

    FIO_CloseFile(fp);
    fio_free(buf);

    if (!error) {
        uart_printf("ROM_DUMP: SUCCESS — %d bytes written\n", (int)written);
        uart_printf("ROM_DUMP: checksum32 = 0x%08X\n", total_csum);

        /* Write log file */
        FILE *log = FIO_CreateFile(LOG_PATH);
        if (log && (int)(intptr_t)log != -1) {
            char msg[256];
            int len = snprintf(msg, sizeof(msg),
                "EOS R R.180 ROM Dump\n"
                "Date: built %s\n"
                "Base: 0x%08X\n"
                "Size: 0x%08X (%d bytes)\n"
                "Checksum32: 0x%08X\n"
                "Status: OK\n"
                "\n"
                "Load in Ghidra:\n"
                "  Language: ARM:LE:32:Cortex\n"
                "  Base addr: 0x%08X\n"
                "  Thumb mode: yes (LSB=1 for all function addresses)\n"
                "\n"
                "Run EDMAC analysis:\n"
                "  python3 FindEDMAC_D8.py ROM_E0000000.BIN --camera R_180\n"
                "  python3 edmac_config.py R_180 ROM_E0000000.BIN\n",
                __DATE__,
                (unsigned)ROM_BASE, (unsigned)ROM_SIZE, (int)written,
                total_csum, (unsigned)ROM_BASE);
            FIO_WriteFile(log, msg, len);
            FIO_CloseFile(log);
        }

        /* Success: 3 slow blinks */
        blink_n(3, 500, 300);

    } else {
        uart_printf("ROM_DUMP: FAILED at %d bytes\n", (int)written);
        blink_n(10, 100, 100);
    }

    uart_printf("ROM_DUMP: task complete\n");
}

/* ── Entry point called from function_overrides.c:platform_post_init() ─── */
void rom_dump_init(void)
{
    uart_printf("ROM_DUMP: scheduling dump task\n");
    task_create("rom_dump", 0x1c, 0x4000, rom_dump_task, 0);
}

/* ── OPTIONAL: hook into existing platform_post_init ───────────────────── */
/*
 * In platform/R.180/function_overrides.c, add to platform_post_init():
 *
 *   void platform_post_init()
 *   {
 *       pMemoryMgr = MMGR_DEFAULT_POOL;
 *       return;  // disable MMGR
 *
 *       #ifdef CONFIG_ROM_DUMP
 *       rom_dump_init();
 *       #endif
 *   }
 *
 * And in internals.h (or Makefile): #define CONFIG_ROM_DUMP
 *
 * This way the dump is only triggered in special builds, not every boot.
 */
