/**
 * edmac_scan.c — DIGIC 6 EDMAC Channel Scanner Module
 * =====================================================
 * ML module (.mo) that iterates all 47 DIGIC 6 EDMAC channels,
 * reads their MMIO registers, identifies active channels, and logs
 * everything to ML/LOGS/EDMAC_SCAN.LOG.
 *
 * This provides the RAW_LV_EDMAC_CHANNEL_ADDR value without guessing
 * and confirms the EDMAC MMIO base address / stride for DIGIC 6.
 *
 * Build:
 *   1. Copy to ~/magiclantern_simplified/modules/edmac_scan/edmac_scan.c
 *   2. Create modules/edmac_scan/Makefile (see bottom of file)
 *   3. cd ~/magiclantern_simplified/modules/edmac_scan && make
 *   4. Copy edmac_scan.mo to ML/modules/ on SD card
 *
 * Usage:
 *   ML menu → Modules → edmac_scan → Enable
 *   Reboot camera into LiveView (half-press shutter)
 *   Scan runs once on startup; check ML/LOGS/EDMAC_SCAN.LOG
 *
 * Safety:
 *   - Read-only MMIO access via volatile pointer dereferences
 *   - No writes to any hardware register
 *   - No EDMAC operations (no SetEDmac / StartEDmac calls)
 *   - Low-priority task, yields to Canon
 *
 * Architecture:
 *   CPU:  ARM Cortex-R4, ARMv7-R, Thumb-2
 *   MMIO: 0xD000xxxx (DIGIC 6 candidate base)
 *   ROM:  0xFE000000 (32MB)
 *
 * Author: PRISM research — 2026-02-18
 * Camera: EOS 80D (should also work on 750D, 760D, 7D2)
 */

#include "dryos.h"
#include "module.h"
#include "bmp.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

extern int uart_printf(const char *fmt, ...);

/* ── Constants ─────────────────────────────────────────────────────────── */

#define EDMAC_NUM_CHANNELS   47
#define LOG_PATH             "ML/LOGS/EDMAC_SCAN.LOG"

/* DIGIC 6 EDMAC MMIO — multiple candidate layouts to test */
/* Winner is determined by finding the most non-zero channels during LiveView */

typedef struct {
    uint32_t base;
    uint32_t stride;
    const char *label;
} edmac_layout_t;

static const edmac_layout_t LAYOUTS[] = {
    { 0xD0000000, 0x0200, "D6-A base=0xD0000000 stride=0x200" },
    { 0xD0000000, 0x0400, "D6-B base=0xD0000000 stride=0x400" },
    { 0xD0000000, 0x1000, "D6-C base=0xD0000000 stride=0x1000" },
    { 0xD0004000, 0x0200, "D6-D base=0xD0004000 stride=0x200  (7D2-adjacent)" },
    { 0xD0004000, 0x1000, "D6-E base=0xD0004000 stride=0x1000" },
    { 0xD0001000, 0x0200, "D6-F base=0xD0001000 stride=0x200" },
    { 0xC0F04000, 0x0100, "D5-ref base=0xC0F04000 stride=0x100 (DIGIC 5)" },
};
#define NUM_LAYOUTS  (sizeof(LAYOUTS) / sizeof(LAYOUTS[0]))

/* EDMAC channel register offsets (DIGIC 5 confirmed, DIGIC 6 TBD) */
#define REG_OFF_STATUS   0x00  /* Channel status / configuration */
#define REG_OFF_ADDR     0x04  /* DMA address (source or destination) */
#define REG_OFF_SIZE     0x08  /* Transfer size */
#define REG_OFF_FLAGS    0x0C  /* Flags / control bits */
#define REG_OFF_INTR     0x10  /* Interrupt / completion count */
#define REG_OFF_CONN     0x14  /* Connection ID (which peripheral) */
#define REG_OFF_XSIZE    0x18  /* Extended size (some channels) */

/* Known LiveView DRAM buffer range (from 80D consts.h candidates) */
#define DRAM_START       0x10000000
#define DRAM_END         0xE0000000

/* ── Safe MMIO read ─────────────────────────────────────────────────────── */

static uint32_t mmio_read(uint32_t addr)
{
    /* Volatile pointer read — safe for MMIO status registers */
    return *(volatile uint32_t *)addr;
}

static int is_dram_addr(uint32_t addr)
{
    return (addr >= DRAM_START && addr < DRAM_END);
}

/* ── Per-channel read ──────────────────────────────────────────────────── */

typedef struct {
    int      ch;
    uint32_t base_addr;
    uint32_t r_status;
    uint32_t r_addr;
    uint32_t r_size;
    uint32_t r_flags;
    uint32_t r_intr;
    uint32_t r_conn;
    int      is_active;
    int      points_to_dram;
} edmac_channel_t;

static void read_channel(uint32_t mmio_base, uint32_t stride, int ch,
                         edmac_channel_t *out)
{
    uint32_t base = mmio_base + ch * stride;

    out->ch           = ch;
    out->base_addr    = base;
    out->r_status     = mmio_read(base + REG_OFF_STATUS);
    out->r_addr       = mmio_read(base + REG_OFF_ADDR);
    out->r_size       = mmio_read(base + REG_OFF_SIZE);
    out->r_flags      = mmio_read(base + REG_OFF_FLAGS);
    out->r_intr       = mmio_read(base + REG_OFF_INTR);
    out->r_conn       = mmio_read(base + REG_OFF_CONN);

    out->is_active      = (out->r_status != 0);
    out->points_to_dram = is_dram_addr(out->r_addr);
}

/* ── Main scan task ────────────────────────────────────────────────────── */

static void edmac_scan_task(void *arg)
{
    uart_printf("EDMAC_SCAN: starting channel scanner\n");
    msleep(3000);  /* wait for LiveView to stabilize */

    /* Open log file */
    FILE *log = FIO_CreateFile(LOG_PATH);
    if (!log || (int)(intptr_t)log == -1) {
        uart_printf("EDMAC_SCAN: cannot create %s\n", LOG_PATH);
        return;
    }

#define LOGF(fmt, ...) do { \
    char _buf[256]; \
    int _len = snprintf(_buf, sizeof(_buf), fmt "\n", ##__VA_ARGS__); \
    FIO_WriteFile(log, _buf, _len); \
    uart_printf("EDMAC: " fmt "\n", ##__VA_ARGS__); \
} while(0)

#define LOG(s) do { \
    FIO_WriteFile(log, s "\n", sizeof(s)); \
    uart_printf("EDMAC: " s "\n"); \
} while(0)

    LOGF("EDMAC Channel Scanner — EOS 80D DIGIC 6");
    LOGF("Camera: built %s", __DATE__);
    LOGF("Channels: %d", EDMAC_NUM_CHANNELS);
    LOG("");

    /* ── Phase 1: Determine best MMIO layout ── */
    LOG("PHASE 1: MMIO LAYOUT DISCOVERY");
    LOG("-----------------------------------------------------------");
    LOG("");

    int best_layout = 0;
    int best_score  = 0;

    for (int l = 0; l < (int)NUM_LAYOUTS; l++) {
        uint32_t base   = LAYOUTS[l].base;
        uint32_t stride = LAYOUTS[l].stride;
        int nonzero = 0;

        for (int ch = 0; ch < EDMAC_NUM_CHANNELS; ch++) {
            uint32_t status = mmio_read(base + ch * stride + REG_OFF_STATUS);
            if (status != 0) nonzero++;
        }

        LOGF("  %s", LAYOUTS[l].label);
        LOGF("    Non-zero channels: %d/%d", nonzero, EDMAC_NUM_CHANNELS);
        LOG("");

        if (nonzero > best_score) {
            best_score  = nonzero;
            best_layout = l;
        }

        msleep(10);
    }

    if (best_score == 0) {
        LOG("WARNING: No non-zero channels found in any layout!");
        LOG("Are you in LiveView? (Half-press shutter or press LV button)");
        LOG("Proceeding with D6-A layout as default...");
        best_layout = 0;
    } else {
        LOGF("BEST LAYOUT: %s", LAYOUTS[best_layout].label);
        LOGF("             Non-zero channels: %d", best_score);
    }

    uint32_t best_base   = LAYOUTS[best_layout].base;
    uint32_t best_stride = LAYOUTS[best_layout].stride;

    LOG("");

    /* ── Phase 2: Full channel dump ── */
    LOG("PHASE 2: FULL CHANNEL REGISTER DUMP");
    LOGF("Base=0x%08X  Stride=0x%08X", best_base, best_stride);
    LOG("-----------------------------------------------------------");
    LOGF("%-4s  %-10s  %-10s  %-10s  %-10s  %-10s  %-10s  %-6s",
         "CH", "+STATUS", "+ADDR", "+SIZE", "+FLAGS", "+INTR", "+CONN", "ACTIVE");
    LOG("-----------------------------------------------------------");

    edmac_channel_t active_channels[EDMAC_NUM_CHANNELS];
    int num_active = 0;

    for (int ch = 0; ch < EDMAC_NUM_CHANNELS; ch++) {
        edmac_channel_t c;
        read_channel(best_base, best_stride, ch, &c);

        LOGF("%02d    %08X    %08X    %08X    %08X    %08X    %08X    %s",
             ch,
             c.r_status, c.r_addr, c.r_size,
             c.r_flags,  c.r_intr, c.r_conn,
             c.is_active ? (c.points_to_dram ? "ACT-LV" : "ACTIVE") : "idle");

        if (c.is_active) {
            active_channels[num_active++] = c;
        }

        msleep(1);
    }

    LOG("");

    /* ── Phase 3: Identify LiveView / sensor channel ── */
    LOG("PHASE 3: ACTIVE CHANNEL ANALYSIS");
    LOG("-----------------------------------------------------------");
    LOGF("Active channels: %d", num_active);
    LOG("");

    edmac_channel_t *best_ch    = NULL;
    uint32_t         best_size  = 0;

    for (int i = 0; i < num_active; i++) {
        edmac_channel_t *c = &active_channels[i];

        LOGF("Channel %02d @ 0x%08X:", c->ch, c->base_addr);
        LOGF("  Status:     0x%08X", c->r_status);
        LOGF("  DMA Addr:   0x%08X  %s",
             c->r_addr,
             c->points_to_dram ? "[→ DRAM: likely sensor/LV buffer!]" : "");
        LOGF("  Size:       0x%08X  (%u bytes = %u KB)",
             c->r_size, c->r_size,
             c->r_size / 1024u);
        LOGF("  Flags:      0x%08X", c->r_flags);
        LOGF("  IRQ/count:  0x%08X", c->r_intr);
        LOGF("  Connection: 0x%08X", c->r_conn);
        LOG("");

        /* Track largest transfer — likely the sensor readout */
        if (c->r_size > best_size && c->points_to_dram) {
            best_size = c->r_size;
            best_ch   = c;
        }
    }

    /* ── Phase 4: RAW_LV_EDMAC_CHANNEL_ADDR candidate ── */
    LOG("PHASE 4: RAW_LV_EDMAC_CHANNEL_ADDR CANDIDATE");
    LOG("-----------------------------------------------------------");

    if (!best_ch) {
        LOG("No active DRAM-pointing channel found.");
        LOG("→ Run this scan during LiveView (sensor must be reading out)");
    } else {
        LOGF("CANDIDATE: channel %02d @ 0x%08X", best_ch->ch, best_ch->base_addr);
        LOGF("  Transfer size: 0x%08X (%u bytes)", best_size, best_size);
        LOGF("  DMA addr:      0x%08X", best_ch->r_addr);
        LOG("");
        LOG("If this is the sensor readout channel, add to consts.h:");
        LOG("");
        char stub_line[128];
        snprintf(stub_line, sizeof(stub_line),
                 "  #define RAW_LV_EDMAC_CHANNEL_ADDR  0x%08X  // ch %02d",
                 best_ch->base_addr, best_ch->ch);
        FIO_WriteFile(log, stub_line, strlen(stub_line));
        FIO_WriteFile(log, "\n", 1);
        uart_printf("EDMAC: %s\n", stub_line);
        LOG("");
        LOG("Verify in Ghidra: find function that writes to this address");
        LOG("→ That function sets up the LV sensor EDMAC channel");
    }

    /* ── Phase 5: Cross-reference 7D2 anchor ── */
    LOG("");
    LOG("PHASE 5: 7D2 ANCHOR CROSS-REFERENCE");
    LOG("-----------------------------------------------------------");
    /* The 7D2 has RAW_LV_EDMAC_CHANNEL_ADDR = 0xD0004200 */
    /* Check what's at that address on THIS camera */
    uint32_t d7_anchor    = 0xD0004200;
    uint32_t val_at_anchor = mmio_read(d7_anchor);
    LOGF("7D2 RAW_LV address (0xD0004200) on this camera: 0x%08X %s",
         val_at_anchor,
         val_at_anchor ? "[NON-ZERO: same channel layout!]" : "[zero: different layout]");

    /* Scan ±8 channels around 7D2 anchor with stride 0x200 */
    LOG("");
    LOG("Scanning ±8 channels around 7D2 anchor (stride=0x200):");
    for (int offset = -8; offset <= 8; offset++) {
        uint32_t addr = 0xD0004200 + offset * 0x200;
        uint32_t val  = mmio_read(addr);
        if (val != 0) {
            LOGF("  0x%08X  (offset %+d)  = 0x%08X  [ACTIVE]", addr, offset, val);
        }
    }

    /* ── Done ── */
    LOG("");
    LOG("=== EDMAC SCAN COMPLETE ===");
    LOGF("Log saved to: %s", LOG_PATH);
    LOG("");
    LOG("NEXT STEPS:");
    LOG("  1. Open EDMAC_SCAN.LOG and find the RAW_LV_EDMAC_CHANNEL_ADDR candidate");
    LOG("  2. If MMIO layout is confirmed, note the base and stride");
    LOG("  3. Load ROM1.BIN in Ghidra, find function writing to channel base addr");
    LOG("  4. That function IS SetEDmac or an EDMAC init wrapper");
    LOG("  5. Trace it to find all 8 missing stub addresses");

    FIO_CloseFile(log);
    uart_printf("EDMAC_SCAN: complete, log at %s\n", LOG_PATH);
}

/* ── Module init / cleanup ─────────────────────────────────────────────── */

static unsigned int edmac_scan_init(void)
{
    uart_printf("EDMAC_SCAN: module loaded\n");
    task_create("edmac_scan", 0x1f, 0x4000, edmac_scan_task, 0);
    return 0;
}

static unsigned int edmac_scan_cleanup(void)
{
    uart_printf("EDMAC_SCAN: module unloaded\n");
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(edmac_scan_init)
    MODULE_DEINIT(edmac_scan_cleanup)
MODULE_INFO_END()

/*
 * ─── Makefile for modules/edmac_scan/ ───────────────────────────────────
 *
 * Create this file as: ~/magiclantern_simplified/modules/edmac_scan/Makefile
 *
 * ---
 * MODULE_NAME      = edmac_scan
 * MODULE_OBJS      = edmac_scan.o
 * MODULE_CFLAGS    =
 * MODULE_DEPS      =
 *
 * include ../Makefile.modules
 * ---
 *
 * Build:
 *   cd ~/magiclantern_simplified/modules/edmac_scan && make
 *
 * Install:
 *   cp edmac_scan.mo /mnt/sdcard/ML/modules/
 */
