/**
 * rom0_dump.c — Canon 80D (DIGIC 6) standalone ROM dumper
 *
 * Dumps the full 32MB ROM (0xFC000000–0xFDFFFFFF) to SD card as ROM0.BIN.
 * On DIGIC 6, there is no separate ROM0 chip — ROM1 at 0xFC000000 is the
 * only ROM, mirrored at 0xFE000000. We dump from 0xFC000000 to get
 * addresses matching Ghidra base.
 *
 * Build:  cd ~/magiclantern_simplified/platform/80D.103 && make rom_dump
 * Deploy: Copy build/rom_dump/autoexec.bin to SD card root
 * Run:    Boot 80D with bootflag set → dump runs automatically
 * Output: B:/ROM0.BIN (32MB) + B:/ROM_DUMP_LOG.TXT
 *
 * Architecture: DIGIC 6 / Cortex-R4 / Thumb-2 / ARMv7-R
 * ROM base:     0xFC000000 (32MB, same as ROM1_ADDR)
 * LED:          CARD_LED_ADDRESS = 0xD20B0A24
 *
 * Safety: read-only access to ROM, only writes to SD card.
 * Uses Canon FIO stubs directly, no ML memory allocator.
 */

#ifdef CONFIG_ROM_DUMP_ONLY

#include "dryos.h"
#include <string.h>

extern int uart_printf(const char *fmt, ...);

/* Canon FIO stubs from stubs.S */
extern FILE* _FIO_CreateFile(const char *name);
extern int _FIO_WriteFile(FILE *f, const void *buf, size_t count);
extern void FIO_CloseFile(FILE *f);

/* Canon memory stubs from stubs.S */
extern void *_alloc_dma_memory(size_t size);
extern void _free_dma_memory(void *ptr);

/* Hardware constants */
#define CARD_LED_ADDRESS    0xD20B0A24
#define LEDON               0x4D0002
#define LEDOFF              0x4C0003

/* Dump parameters */
#define ROM_BASE            0xFC000000
#define ROM_SIZE            0x02000000  /* 32MB */
#define CHUNK_SIZE          0x00100000  /* 1MB per write */
#define DUMP_PATH           "B:/ROM0.BIN"
#define LOG_PATH            "B:/ROM_DUMP_LOG.TXT"

/* LED helpers */
static void led_on(void)  { *(volatile uint32_t *)CARD_LED_ADDRESS = LEDON; }
static void led_off(void) { *(volatile uint32_t *)CARD_LED_ADDRESS = LEDOFF; }

static void blink_n(int n, int ms_on, int ms_off)
{
    for (int i = 0; i < n; i++) {
        led_on();  msleep(ms_on);
        led_off(); msleep(ms_off);
    }
}

/* Simple 32-bit checksum for verification */
static uint32_t checksum32(const void *buf, size_t len)
{
    const uint32_t *p = (const uint32_t *)buf;
    uint32_t sum = 0;
    for (size_t i = 0; i < len / 4; i++)
        sum += p[i];
    return sum;
}

/* Main dump task — uses Canon DryOS FIO, no ML allocator */
void rom_dump_task(void *unused)
{
    (void)unused;

    uart_printf("[ROM_DUMP] 80D ROM dumper starting\n");
    uart_printf("[ROM_DUMP] base=0x%08X size=0x%08X (%dMB)\n",
                ROM_BASE, ROM_SIZE, ROM_SIZE >> 20);

    msleep(3000);  /* let DryOS settle */

    /* Allocate 1MB DMA-safe buffer via Canon's allocator */
    uint8_t *buf = (uint8_t *)_alloc_dma_memory(CHUNK_SIZE);
    if (!buf) {
        uart_printf("[ROM_DUMP] ERROR: _alloc_dma_memory(%d) failed\n", CHUNK_SIZE);
        blink_n(10, 100, 100);
        return;
    }
    uart_printf("[ROM_DUMP] buffer at %p\n", buf);

    /* Signal start: 2 slow blinks */
    blink_n(2, 300, 200);

    /* Create output file */
    FILE *fp = _FIO_CreateFile(DUMP_PATH);
    if (!fp || (int)(intptr_t)fp == -1) {
        uart_printf("[ROM_DUMP] ERROR: cannot create %s\n", DUMP_PATH);
        _free_dma_memory(buf);
        blink_n(10, 100, 100);
        return;
    }
    uart_printf("[ROM_DUMP] writing to %s\n", DUMP_PATH);

    /* Dump ROM in 1MB chunks */
    size_t written = 0;
    uint32_t total_csum = 0;
    int error = 0;

    for (size_t offset = 0; offset < ROM_SIZE; offset += CHUNK_SIZE) {
        size_t chunk = CHUNK_SIZE;
        if (offset + chunk > ROM_SIZE)
            chunk = ROM_SIZE - offset;

        /* Read from ROM into DMA buffer */
        memcpy(buf, (const void *)(ROM_BASE + offset), chunk);

        /* Accumulate checksum */
        total_csum += checksum32(buf, chunk);

        /* Write to SD */
        int ret = _FIO_WriteFile(fp, buf, chunk);
        if (ret != (int)chunk) {
            uart_printf("[ROM_DUMP] ERROR: write failed at offset 0x%08X (ret=%d)\n",
                        (unsigned)offset, ret);
            error = 1;
            break;
        }

        written += chunk;

        /* Progress: blink every 4MB */
        if ((offset % 0x400000) == 0) {
            led_on(); msleep(30); led_off();
            uart_printf("[ROM_DUMP] %d/%d MB\n",
                        (int)(offset >> 20), (int)(ROM_SIZE >> 20));
        }

        msleep(1);  /* yield to Canon tasks */
    }

    FIO_CloseFile(fp);

    if (!error) {
        uart_printf("[ROM_DUMP] SUCCESS: %d bytes written\n", (int)written);
        uart_printf("[ROM_DUMP] checksum32 = 0x%08X\n", total_csum);

        /* Write verification log */
        FILE *log = _FIO_CreateFile(LOG_PATH);
        if (log && (int)(intptr_t)log != -1) {
            char msg[512];
            int len = snprintf(msg, sizeof(msg),
                "Canon 80D (DIGIC 6) ROM Dump\n"
                "Firmware: 1.0.3\n"
                "Built: %s %s\n"
                "Base: 0x%08X\n"
                "Size: 0x%08X (%d bytes)\n"
                "Checksum32: 0x%08X\n"
                "Status: OK\n"
                "\n"
                "NOTE: DIGIC 6 has no separate ROM0.\n"
                "ROM1 at 0xFC000000 is the only ROM (32MB).\n"
                "0xFE000000 is a mirror of the same data.\n"
                "\n"
                "Ghidra setup:\n"
                "  Language: ARM:LE:32:Cortex\n"
                "  Processor: ARM Cortex-R4\n"
                "  Base address: 0xFC000000\n"
                "  Thumb mode: yes (add 1 to all function addresses)\n",
                __DATE__, __TIME__,
                (unsigned)ROM_BASE, (unsigned)ROM_SIZE, (int)written,
                total_csum);
            _FIO_WriteFile(log, msg, len);
            FIO_CloseFile(log);
        }

        /* Success: 3 slow blinks */
        blink_n(3, 500, 300);
    } else {
        uart_printf("[ROM_DUMP] FAILED at %d bytes\n", (int)written);
        blink_n(10, 100, 100);
    }

    _free_dma_memory(buf);
    uart_printf("[ROM_DUMP] task complete\n");
}

#endif /* CONFIG_ROM_DUMP_ONLY */
