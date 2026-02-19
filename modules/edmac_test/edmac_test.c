/**
 * edmac_test.c — QEMU EDMAC Synthetic Capture Test Module
 * =========================================================
 * Minimal ML module that exercises the QEMU EDMAC harness by:
 *   1. Allocating a destination buffer in cached DRAM
 *   2. Writing EDMAC channel 2 registers directly (bypasses Canon firmware)
 *   3. Starting the transfer (triggers QEMU synthetic fill)
 *   4. Polling for completion
 *   5. Saving the captured data to the SD card
 *
 * This module talks directly to QEMU's MMIO intercept layer.
 * It does NOT use Canon's EDMAC firmware functions, so it works
 * regardless of whether the Canon stubs are correctly mapped.
 *
 * Enable the QEMU harness with:
 *   QEMU_EOS_SYNTHETIC_EDMAC=2
 *
 * Output:
 *   ML/LOGS/EDMAC_TEST.RAW  — raw synthetic Bayer data
 *   UART log messages with [EDMAC_TEST] prefix
 *
 * Camera: EOS 80D (DIGIC 6)
 * Author: PRISM research — 2026-02-19
 */

#include "dryos.h"
#include "module.h"
#include <string.h>

/* Use ML's printf (goes through console → UART via CONFIG_COPY_CONSOLE_TO_UART).
 * Canon's uart_printf at 0xfe483d01 may not be visible in QEMU's log output. */

/* EDMAC channel 2 MMIO base (RAW LiveView channel on 80D) */
#define EDMAC_CH2_BASE      0xD0004200

/* Synthetic frame dimensions (LiveView-sized) */
#define FRAME_WIDTH         1872
#define FRAME_HEIGHT        1060
#define BITS_PER_PIXEL      14
#define LINE_BYTES          ((FRAME_WIDTH * BITS_PER_PIXEL + 7) / 8)  /* 3276 */

/* EDMAC register offsets */
#define REG_CONTROL         0x00
#define REG_FLAGS           0x04
#define REG_ADDR            0x08
#define REG_YN_XN           0x0C
#define REG_YB_XB           0x10
#define REG_YA_XA           0x14
#define REG_OFF1B           0x18
#define REG_OFF2B           0x1C
#define REG_OFF1A           0x20
#define REG_OFF2A           0x24
#define REG_OFF3            0x28
#define REG_IRQ_REASON      0x30
#define REG_ABORT           0x34
#define REG_CONNECTION      0x40

static inline void edmac_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(EDMAC_CH2_BASE + offset) = value;
}

static inline uint32_t edmac_read(uint32_t offset)
{
    return *(volatile uint32_t *)(EDMAC_CH2_BASE + offset);
}

static void edmac_test_task(void *unused)
{
    printf("[EDMAC_TEST] Starting synthetic capture test in 1 second...\n");
    msleep(1000);

    /* Write progress marker to prove task is running */
    {
        FILE *f2 = FIO_CreateFile("ML/LOGS/EDMAC_TASK.TXT");
        if (f2 && (int)(intptr_t)f2 != -1) {
            const char *msg2 = "edmac_test task started, about to configure EDMAC\n";
            FIO_WriteFile(f2, msg2, strlen(msg2));
            FIO_CloseFile(f2);
        }
    }

    /* Calculate buffer size */
    uint32_t buf_size = (uint32_t)LINE_BYTES * FRAME_HEIGHT;
    printf("[EDMAC_TEST] Frame: %dx%d, %d bpp, %d bytes/line, total %u bytes\n",
                FRAME_WIDTH, FRAME_HEIGHT, BITS_PER_PIXEL, LINE_BYTES, buf_size);

    /* Allocate destination buffer in cached DRAM */
    void *buf = malloc(buf_size + 256);  /* extra padding */
    if (!buf) {
        printf("[EDMAC_TEST] ERROR: malloc(%u) failed\n", buf_size + 256);
        return;
    }

    /* Align to 64 bytes (DMA alignment) */
    void *aligned_buf = (void *)(((uint32_t)buf + 63) & ~63);
    printf("[EDMAC_TEST] Buffer: raw=0x%08X aligned=0x%08X\n",
                (uint32_t)buf, (uint32_t)aligned_buf);

    /* Zero the buffer to detect writes */
    memset(aligned_buf, 0, buf_size);

    /* ── Configure EDMAC channel 2 registers ── */

    /* Clear any previous state */
    edmac_write(REG_IRQ_REASON, 0);
    edmac_write(REG_ABORT, 0);

    /* Set destination address (cached DRAM pointer) */
    edmac_write(REG_ADDR, (uint32_t)aligned_buf);

    /* Set transfer geometry: yn|xn
     * xn = bytes per line, yn = number of lines minus 1 */
    edmac_write(REG_YN_XN, ((FRAME_HEIGHT - 1) << 16) | LINE_BYTES);

    /* Set sub-block geometry (zeros for simple contiguous transfer) */
    edmac_write(REG_YB_XB, 0);
    edmac_write(REG_YA_XA, 0);

    /* Set row stride = bytes per line (contiguous, no gaps) */
    edmac_write(REG_OFF1A, LINE_BYTES);
    edmac_write(REG_OFF1B, 0);
    edmac_write(REG_OFF2A, 0);
    edmac_write(REG_OFF2B, 0);
    edmac_write(REG_OFF3, 0);

    /* Set connection (arbitrary, harness doesn't use it) */
    edmac_write(REG_CONNECTION, 0);

    printf("[EDMAC_TEST] Registers configured. Starting transfer...\n");

    /* ── START the transfer ── */
    /* Writing 1 to control register triggers StartEDmac in QEMU harness */
    edmac_write(REG_CONTROL, 1);

    /* ── Poll for completion ── */
    /* The QEMU harness fills the buffer synchronously during the control write,
     * so IRQ reason should already be 0x02 by the time we read it.
     * The timeout is just a safety net. */
    int timeout = 100;
    uint32_t irq;
    while (timeout > 0) {
        irq = edmac_read(REG_IRQ_REASON);
        if (irq & 0x02) break;
        msleep(10);
        timeout--;
    }

    if (!(irq & 0x02)) {
        printf("[EDMAC_TEST] ERROR: Timeout waiting for transfer (IRQ=0x%X)\n", irq);
        printf("[EDMAC_TEST] Is QEMU_EOS_SYNTHETIC_EDMAC=2 set?\n");
        free(buf);
        return;
    }

    printf("[EDMAC_TEST] Transfer complete! IRQ reason = 0x%02X\n", irq);

    /* ── Verify buffer contents ── */
    uint8_t *data = (uint8_t *)aligned_buf;
    int nonzero_bytes = 0;
    uint32_t first_nonzero_offset = 0;
    uint8_t first_nonzero_value = 0;

    for (uint32_t i = 0; i < buf_size; i++) {
        if (data[i] != 0) {
            if (nonzero_bytes == 0) {
                first_nonzero_offset = i;
                first_nonzero_value = data[i];
            }
            nonzero_bytes++;
        }
    }

    printf("[EDMAC_TEST] Buffer check: %d/%u bytes non-zero (%d%%)\n",
                nonzero_bytes, buf_size,
                buf_size ? (nonzero_bytes * 100 / (int)buf_size) : 0);
    if (nonzero_bytes > 0) {
        printf("[EDMAC_TEST] First non-zero: offset=%u value=0x%02X\n",
                    first_nonzero_offset, first_nonzero_value);
    }

    /* Quick pattern check: sample 5 points across the first line (top bar region) */
    printf("[EDMAC_TEST] Top line samples: ");
    for (int i = 0; i < 5; i++) {
        uint32_t x = (i * LINE_BYTES) / 5;
        printf("[%u]=0x%02X ", x, data[x]);
    }
    printf("\n");

    /* Sample middle line (gradient region) */
    uint32_t mid_y = FRAME_HEIGHT / 2;
    uint32_t mid_offset = mid_y * LINE_BYTES;
    printf("[EDMAC_TEST] Mid line (y=%u) samples: ", mid_y);
    for (int i = 0; i < 5; i++) {
        uint32_t x = (i * LINE_BYTES) / 5;
        printf("[%u]=0x%02X ", x, data[mid_offset + x]);
    }
    printf("\n");

    /* Sample bottom line (checkerboard region) */
    uint32_t bot_y = FRAME_HEIGHT - 10;
    uint32_t bot_offset = bot_y * LINE_BYTES;
    printf("[EDMAC_TEST] Bot line (y=%u) samples: ", bot_y);
    for (int i = 0; i < 8; i++) {
        uint32_t x = i * 8; /* sample each 8-byte block boundary */
        printf("[%u]=0x%02X ", x, data[bot_offset + x]);
    }
    printf("\n");

    /* ── Save to SD card ── */
    FILE *f = FIO_CreateFile("ML/LOGS/EDMAC_TEST.RAW");
    if (f && (int)(intptr_t)f != -1) {
        FIO_WriteFile(f, aligned_buf, buf_size);
        FIO_CloseFile(f);
        printf("[EDMAC_TEST] Saved %u bytes to ML/LOGS/EDMAC_TEST.RAW\n", buf_size);
    } else {
        printf("[EDMAC_TEST] ERROR: Cannot create ML/LOGS/EDMAC_TEST.RAW\n");
    }

    /* Also save a small header file with metadata */
    f = FIO_CreateFile("ML/LOGS/EDMAC_TEST.TXT");
    if (f && (int)(intptr_t)f != -1) {
        char hdr[256];
        int len = snprintf(hdr, sizeof(hdr),
            "EDMAC Synthetic Capture\n"
            "Width: %d\n"
            "Height: %d\n"
            "BPP: %d\n"
            "LineBytes: %d\n"
            "TotalBytes: %u\n"
            "NonZeroBytes: %d\n"
            "BufferAddr: 0x%08X\n",
            FRAME_WIDTH, FRAME_HEIGHT, BITS_PER_PIXEL, LINE_BYTES,
            buf_size, nonzero_bytes, (uint32_t)aligned_buf);
        FIO_WriteFile(f, hdr, len);
        FIO_CloseFile(f);
        printf("[EDMAC_TEST] Metadata saved to ML/LOGS/EDMAC_TEST.TXT\n");
    }

    free(buf);
    printf("[EDMAC_TEST] === TEST COMPLETE ===\n");
}

static unsigned int edmac_test_init(void)
{
    /* Write a marker file immediately to prove module init ran */
    FILE *marker = FIO_CreateFile("ML/LOGS/EDMAC_INIT.TXT");
    if (marker && (int)(intptr_t)marker != -1) {
        const char *msg = "edmac_test module init called\n";
        FIO_WriteFile(marker, msg, strlen(msg));
        FIO_CloseFile(marker);
    }
    printf("[EDMAC_TEST] Module loaded\n");
    task_create("edmac_test", 0x1f, 0x4000, edmac_test_task, 0);
    return 0;
}

static unsigned int edmac_test_cleanup(void)
{
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(edmac_test_init)
    MODULE_DEINIT(edmac_test_cleanup)
MODULE_INFO_END()
