/** \file
 * Function overrides needed for 750D 1.1.0
 */
/*
 * Copyright (C) 2021 Magic Lantern Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <dryos.h>
#include <property.h>
#include <bmp.h>
#include <config.h>
#include <consts.h>
#include <lens.h>
#include <edmac.h>
#include "mem.h"

extern int uart_printf(const char * fmt, ...);

// fake WINSYS_BMP_DIRTY_BIT_NEG
int winsys_bmp_dirty_bit_neg = 0;

void LoadCalendarFromRTC(struct tm *tm)
{
    // differs from D78, one arg is missing
    _LoadCalendarFromRTC(tm, 0, 16);
}

/*
 * Partition tables stuff. Copied from R as I was too lazy to search for stub.
 * And clean implementation in ML code is worth it anyway I guess.
 */

struct chs_entry
{
  uint8_t head;
  uint8_t sector; //sector + cyl_msb
  uint8_t cyl_lsb;
}__attribute__((packed));

struct partition
{
  uint8_t  state;
  struct chs_entry start;
  uint8_t  type;
  struct chs_entry end;
  uint32_t start_sector;
  uint32_t size;
}__attribute__((aligned,packed));

struct partition_table
{
    uint8_t  state; // 0x80 = bootable
    uint8_t  start_head;
    uint16_t start_cylinder_sector;
    uint8_t  type;
    uint8_t  end_head;
    uint16_t end_cylinder_sector;
    uint32_t sectors_before_partition;
    uint32_t sectors_in_partition;
}__attribute__((packed));

void fsuDecodePartitionTable(void * partIn, struct partition_table * pTable){
    struct partition * part = (struct partition *) partIn;
    pTable->state      = part->state;
    pTable->type       = part->type;
    pTable->start_head = part->start.head;
    pTable->end_head   = part->end.head;
    pTable->sectors_before_partition = part->start_sector;
    pTable->sectors_in_partition     = part->size;

    //tricky bits - TBD
    pTable->start_cylinder_sector = 0;
    pTable->end_cylinder_sector   = 0;

    uart_printf("Bootflag: %02x\n", pTable->state);
    uart_printf("Type: %02x\n", pTable->type);
    uart_printf("Head start: %02x end %02x\n", pTable->start_head, pTable->end_head);
    uart_printf("Sector start: %08x size %08x\n", pTable->sectors_before_partition, pTable->sectors_in_partition);
    uart_printf("CS: Start %04x End %04x\n", pTable->start_cylinder_sector, pTable->end_cylinder_sector);
}

int get_task_info_by_id(int unknown_flag, int task_id, void *task_attr)
{
    // task_id is something like two u16s concatenated.  The flag argument,
    // present on D45 but not on D678 allows controlling if the task info request
    // uses the whole thing, or only the low half.
    //
    // ML calls with this set to 1, meaning task_id is used as is,
    // if 0, the high half is masked out first.
    //
    // D678 doesn't have the 1 option, we use the low half as index
    // to find the full value.
    struct task *task = first_task + (task_id & 0xffff);
    return _get_task_info_by_id(task->taskId, task_attr);
}

/*
 * EDMAC function overrides — DIGIC 6 (80D 1.0.3)
 *
 * Canon's EDMAC API has a different signature from ML's EDMAC API.
 * These wrappers bridge the gap.  The Canon function addresses are from
 * ROM1.BIN analysis (research/reference/80d-candidate-stubs.S).
 *
 * Canon EDMAC function cluster @ 0xfe338136–0xfe3384ca.
 * Shared memory helpers:
 *   fn@0xfe420caa = shadowed write(addr, value)
 *   fn@0xfe420cd8 = shadow read(addr)
 *   fn@0xfe420ce4 = locked RMW(addr, AND_mask, OR_value)
 *
 * TODO (after ROM1.BIN dump confirms addresses on hardware):
 *   1. Remove the [POSSIBLE] comments from stubs.S for confirmed fns.
 *   2. Wire ConnectWriteEDmac directly via THUMB_FN (no wrapper needed).
 *   3. Wire AbortEDmac directly via THUMB_FN (sig matches).
 *   4. Find RegisterEDmacCompleteCBR write path (currently no-op).
 */

/* Canon's SetEDmac(channel, &info) — no address or flags argument.
 * ML calls SetEDmac(channel, address, &info, flags).
 * On DIGIC 6, the DMA buffer address is set separately by ConnectWriteEDmac.
 * We ignore 'address' and 'flags' and forward (channel, ptr) to Canon.
 *
 * Canon fn @ 0xfe3382f2 (Thumb+1 = 0xfe3382f3):
 *   push {r4,r5,lr}; sub sp,#0x24
 *   r0=channel, r1=&edmac_info → validates state, processes 11 fields, bulk-writes to mmio+0xc
 */
static void (* const _canon_SetEDmac)(unsigned int ch, struct edmac_info *ptr)
    = (void *)(0xfe3382f2 | 1);   /* Thumb bit set */

void SetEDmac(unsigned int channel, void *address, struct edmac_info *ptr, int flags)
{
    (void)address;   /* handled by ConnectWriteEDmac on DIGIC 6 */
    (void)flags;     /* encoded in edmac_info fields */
    _canon_SetEDmac(channel, ptr);
}

/* Canon's ConnectWriteEDmac(channel, buf_addr) — COMPATIBLE with ML's API.
 * ML's 'where' parameter IS the DMA buffer address on DIGIC 6.
 * Canon strips top 2 bits (bic #0xc0000000) and writes to mmio+8.
 *
 * Canon fn @ 0xfe33820c (Thumb+1 = 0xfe33820d):
 *   push {r4,r5,r6,lr}; checks state; bic r1,r5,#0xc0000000; writes to mmio+8
 *
 * TODO: enable THUMB_FN in stubs.S and remove this wrapper after hardware confirm.
 */
static void (* const _canon_ConnectWriteEDmac)(unsigned int ch, unsigned int buf_addr)
    = (void *)(0xfe33820c | 1);

void ConnectWriteEDmac(unsigned int channel, unsigned int where)
{
    _canon_ConnectWriteEDmac(channel, where);
}

/* ConnectReadEDmac — Canon equivalent not yet confirmed in ROM scan.
 * Read channels (DRAM→camera) are used for playback, not MLV recording.
 * Safe no-op for mlv_lite recording use case.
 */
void ConnectReadEDmac(unsigned int channel, unsigned int where)
{
    (void)channel; (void)where;
}

/* Canon's StartEDmac(channel) — one argument only.
 * ML calls StartEDmac(channel, flags); 'flags' is ignored.
 * Canon fn @ 0xfe338136 (Thumb+1 = 0xfe338137):
 *   cmp r0,#0x19; push {r4,lr}; range checks; RMW(mmio+4,~0x20000000,0x20000000);
 *   dsb sy; writes 1→mmio+0 (starts DMA).
 */
static void (* const _canon_StartEDmac)(unsigned int ch)
    = (void *)(0xfe338136 | 1);

void StartEDmac(unsigned int channel, int flags)
{
    (void)flags;
    _canon_StartEDmac(channel);
}

/* Canon's AbortEDmac(channel) — one argument, matches ML's API.
 * Canon fn @ 0xfe338176 (Thumb+1 = 0xfe338177):
 *   ldr r2,state_table; movs r1,#1; str.w r1,[r2,r0,lsl#2]; (state[ch]=1)
 *   RMW(mmio+4, ~7, 6) — sets abort bits 1|2 in control register.
 *
 * TODO: enable THUMB_FN in stubs.S and remove this wrapper after hardware confirm.
 */
static void (* const _canon_AbortEDmac)(unsigned int ch)
    = (void *)(0xfe338176 | 1);

void AbortEDmac(unsigned int channel)
{
    _canon_AbortEDmac(channel);
}

/* RegisterEDmacCompleteCBR — write path to Canon's callback table not yet found.
 * The completion ISR (fn@0xfe338d09 for ch 0–24) reads from 0x424b8+ch×4
 * and 0x42578+ch×12.  The function that WRITES those entries was not identified
 * in the ROM scan (no LDR resolving to those addresses exists in the Edmac module).
 * Without this working, mlv_lite will start DMA but never receive frame-done
 * notifications — recording will stall.
 * TODO: find the write path via hardware ROM dump + dynamic trace.
 */
void RegisterEDmacCompleteCBR(int channel, void (*cbr)(void*), void* cbr_ctx)
{
    (void)channel; (void)cbr; (void)cbr_ctx;
}

void UnregisterEDmacCompleteCBR(int channel)
{
    (void)channel;
}

void RegisterEDmacAbortCBR(int channel, void (*cbr)(void*), void* cbr_ctx)
{
    (void)channel; (void)cbr; (void)cbr_ctx;
}

void UnregisterEDmacAbortCBR(int channel)
{
    (void)channel;
}

void RegisterEDmacPopCBR(int channel, void (*cbr)(void*), void* cbr_ctx)
{
    (void)channel; (void)cbr; (void)cbr_ctx;
}

void UnregisterEDmacPopCBR(int channel)
{
    (void)channel;
}

// _EngDrvOut and shamem_read are now resolved via stubs.S:
//   _EngDrvOut  @ 0xfe15aec1  (str r1,[r0]; bx lr)
//   shamem_read @ 0xfe15aec5  (ldr r0,[r0]; bx lr)

void _engio_write(uint32_t* reg_list)
{
    return;
}

unsigned int UnLockEngineResources(struct LockEntry *lockEntry)
{
    return 0;
}

/* CreateResLockEntry — Canon address in ROM0 (unreachable from ROM1 without dump).
 * Candidates 0xfe98004d (1-arg wrapper) / 0xfe98008f (4-arg) exist in ROM1 but
 * neither matches ML's 2-arg signature; ROM0 holds the real implementation.
 * Return a dummy non-NULL handle so ASSERT(resLock) passes and modules load.
 * LockEngineResources (called after this) uses UnLockEngineResources no-op path.
 * DMA will still stall on frame-done (RegisterEDmacCompleteCBR not wired).
 * TODO: replace with THUMB_FN once ROM0.BIN confirms address. */
struct LockEntry *CreateResLockEntry(uint32_t *resIds, uint32_t resIdCount)
{
    (void)resIds; (void)resIdCount;
    static uint32_t _dummy_lock_entry[16];
    return (struct LockEntry *)_dummy_lock_entry;
}

#ifdef CONFIG_AUDIO_CONTROLS
#include <audio.h>

/* Audio IC stubs - safe no-ops until ROM1.BIN provides _audio_ic_read/_write addresses.
 * The AK4646 I2C command addresses are unknown on 80D DIGIC 6.
 * These allow CONFIG_AUDIO_CONTROLS to compile without crashing at runtime. */

void _audio_ic_write(unsigned cmd)
{
    (void)cmd;
}

void _audio_ic_read(unsigned cmd, unsigned *result)
{
    (void)cmd;
    if (result) *result = 0;
}

/* Canon sounddev_task: ML replaces it via TASK_OVERRIDE; this noop satisfies the linker.
 * The TASK_OVERRIDE won't match any real Canon task (addresses differ), so it silently
 * skips - Canon's own audio task continues to run unmodified. */
void sounddev_task(void) { }

/* sounddev_active_in: Canon function to start audio input DMA. Noop until stubbed. */
void sounddev_active_in(void (*unlock_func)(void *), void *arg)
{
    (void)unlock_func;
    (void)arg;
}

/* sounddev: Canon global sound device struct. Static zero buffer - sem writes are safe. */
struct sounddev sounddev;

#endif /* CONFIG_AUDIO_CONTROLS */

/* WiFi socket stubs - error-returning placeholders until ROM1.BIN provides addresses.
 * These allow yolo.mo to link against the 80D sym file and load on physical hardware.
 * The NwLime/wlan eventproc chain runs fully via call() at 0xfe48422e; wlan_connect
 * returns -1 (stub) so yolo_init exits after that chain with a logged error - giving
 * useful diagnostics on first boot. Replace with THUMB_FN stubs from ROM1.BIN to
 * activate. Also find the 80D Lime core init poll addr (200D uses 0x1d90c).
 * ROM1.BIN search targets: "socket", "wlan", "NwLime", "LimeDebugMsg". */
#include "ml_socket.h"

int socket_create(int domain, int type, int protocol)
{
    (void)domain; (void)type; (void)protocol;
    return -1; /* needs ROM1.BIN */
}

int socket_bind(int socket, struct sockaddr_in *addr, int addr_len)
{
    (void)socket; (void)addr; (void)addr_len;
    return -1;
}

int socket_connect(int socket, struct sockaddr_in *addr, int addr_len)
{
    (void)socket; (void)addr; (void)addr_len;
    return -1;
}

int socket_listen(int socket, int backlog)
{
    (void)socket; (void)backlog;
    return -1;
}

int socket_accept(int socket, void *addr, int addr_len)
{
    (void)socket; (void)addr; (void)addr_len;
    return -1;
}

int socket_recv(int socket, void *buf, int len, int flags)
{
    (void)socket; (void)buf; (void)len; (void)flags;
    return 0;
}

int socket_send(int socket, void *buf, int len, int flags)
{
    (void)socket; (void)buf; (void)len; (void)flags;
    return 0;
}

void socket_setsockopt(int socket, int level, int option_name,
                       const void *option_value, int option_len)
{
    (void)socket; (void)level; (void)option_name;
    (void)option_value; (void)option_len;
}

int socket_getsockopt(int socket, int level, int option_name,
                      void *option_value, int option_len)
{
    (void)socket; (void)level; (void)option_name;
    (void)option_value; (void)option_len;
    return -1;
}

int socket_shutdown(int socket, int flag)
{
    (void)socket; (void)flag;
    return 0;
}

int socket_close_caller(int converted_socket)
{
    (void)converted_socket;
    return 0;
}

int socket_convertfd(int socket)
{
    return socket; /* passthrough - no fd translation with stub sockets */
}

int wlan_connect(struct wlan_settings *settings)
{
    (void)settings;
    return -1; /* needs ROM1.BIN - triggers "error from wlan_connect: -1" in yolo_init */
}

int nif_setup(int interface)
{
    (void)interface;
    return 0;
}

int set_IP_address(int interface, uint32_t client_IP,
                   uint32_t subnet_mask, uint32_t gateway_IP)
{
    (void)interface; (void)client_IP; (void)subnet_mask; (void)gateway_IP;
    return 0;
}

static void edmac_builtin_test_task(void *unused);  /* forward decl */

#ifdef CONFIG_RAW_LIVEVIEW
/* QEMU LiveView state simulation.
 *
 * In QEMU, Canon's property system doesn't fire PROP_LV_ACTION
 * (display hardware not connected), so `lv` stays 0 and all raw
 * capture paths bail out early.  These helpers let us force LiveView
 * state after boot so mlv_lite and raw overlays can run.
 *
 * Called from edmac_builtin_test_task after ML finishes booting.
 */
/* lv and shooting_mode are declared in propvalues.h (included via dryos.h) */

static void qemu_simulate_lv_state(void)
{
    printf("[QEMU] Simulating LiveView active state\n");

    /* Force LiveView flag — makes lv_running() return true */
    lv = 1;

    /* Force movie mode so is_movie_mode() returns true
     * SHOOTMODE_MOVIE = 3 on most Canon cameras */
    shooting_mode = 3;

    printf("[QEMU] lv=%d shooting_mode=%d\n", lv, shooting_mode);
}

/* wait_lv_frames: normally in state-object.c inside CONFIG_STATE_OBJECT_HOOKS,
 * which is disabled on 80D.  Provide a simple timing-based stub. */
int wait_lv_frames(int num_frames)
{
    /* ~33ms per frame at 30fps */
    msleep(num_frames * 33);
    return 1;
}

/* Vsync simulation task.
 * CONFIG_STATE_OBJECT_HOOKS is disabled on 80D, so the normal
 * vsync_func() in state-object.c never fires.  This task periodically
 * dispatches CBR_VSYNC so mlv_lite's raw_rec_vsync_cbr can run. */
#include <module.h>

static volatile int vsync_sim_running = 0;

static void qemu_vsync_task(void *unused)
{
    (void)unused;
    printf("[QEMU] Vsync simulation task started (30fps)\n");
    vsync_sim_running = 1;

    while (vsync_sim_running)
    {
        if (lv)
        {
            module_exec_cbr(CBR_VSYNC);
            module_exec_cbr(CBR_VSYNC_SETPARAM);
        }
        msleep(33); /* ~30fps */
    }

    printf("[QEMU] Vsync simulation task stopped\n");
}

void qemu_start_vsync_sim(void)
{
    task_create("vsync_sim", 0x1e, 0x2000, qemu_vsync_task, 0);
}
#endif /* CONFIG_RAW_LIVEVIEW */

#ifdef CONFIG_PLATFORM_POST_INIT
/* Pre-initialize RGBA VRAM for QEMU.
 *
 * Canon's GIS compositor / GuiMainTask never populates _rgb_vram_info in
 * emulation (display hardware not connected), so boot_post_init_task blocks
 * forever on:
 *
 *   while (!rgb_vram_preinit()) msleep(100);   <- reads _rgb_vram_info
 *   while (!bmp_vram_raw())     msleep(100);   <- reads rgb_vram_info->bitmap_data
 *
 * Seeding _rgb_vram_info with a fake MARV struct here (called before those
 * loops) lets both loops pass immediately so ml_init can be scheduled.
 *
 * The RGBA buffer is allocated via Canon's raw heap: _AllocateMemory() at
 * ATCM:0x4D6.  This is called BEFORE ML's mem_sem semaphore is created
 * (_mem_init() runs later in my_big_init_task), so we must NOT use ML's
 * malloc() wrapper (__mem_malloc) — it asserts on mem_sem == NULL.
 * _AllocateMemory() returns cached-DRAM addresses (0x0xxxxxxx) that
 * QEMU-EOS maps as normal writable RAM — safe for the 2 MB RGBA frame.
 *
 * We deliberately avoid _alloc_dma_memory (BTCM:0x800062b8): it returns
 * uncached/DMA addresses (0x4xxxxxxx) that QEMU-EOS does not map as writable
 * RAM, causing a QEMU process crash when refresh_yuv_from_rgb writes the
 * full 960×540×4 RGBA frame.
 *
 * We also avoid a file-scope static uint8_t[960*540*4]: that puts 2 MB in BSS
 * (0x207d40–0x40f100), and zero_bss() in copy_and_restart() zeroing 2 MB at
 * startup takes minutes in QEMU (cache disabled, every word hits DRAM).
 */
extern void *_AllocateMemory(size_t size);  /* Canon DryOS heap (ATCM:0x4D6) */

static struct MARV fake_marv;   /* ~32 bytes in BSS, zero-initialised */

void platform_post_init(void)
{
    uint8_t *buf = (uint8_t *)_AllocateMemory(960 * 540 * 4);
    if (!buf) {
        printf("[QEMU] platform_post_init: _AllocateMemory(%u) failed\n",
                    960 * 540 * 4);
        return;
    }

    fake_marv.signature    = 0x5652414D;  /* 'MARV' */
    fake_marv.bitmap_data  = buf;
    fake_marv.opacity_data = NULL;
    fake_marv.flags        = 0x5040100;   /* XIMR_FLAGS_LAYER_RGBA */
    fake_marv.width        = 960;
    fake_marv.height       = 540;
    fake_marv.pmem         = NULL;

    printf("[QEMU] platform_post_init: buf=%p marv=%p\n",
                buf, &fake_marv);
    _rgb_vram_info = &fake_marv;

    /* Spawn EDMAC synthetic capture test task */
    task_create("edmac_test", 0x1f, 0x4000, edmac_builtin_test_task, 0);
    printf("[QEMU] edmac_test task created\n");
}
#endif /* CONFIG_PLATFORM_POST_INIT */

/* Override XimrExe with a no-op for QEMU.
 *
 * Canon's XimrExe (XIMR render-mixer) tries to composite the RGBA overlay
 * into the display via XIMR hardware DMA, which is not emulated in QEMU-EOS.
 * Calling the real ROM function at 0xfe22b2fc crashes redraw_task.
 *
 * The stub in stubs.S is commented out so the linker resolves to this function.
 * Return 0 (success) so bmp.c continues without errors.
 */
int XimrExe(void *ximr_context)
{
    (void)ximr_context;
    return 0;
}

/* ── EDMAC Synthetic Capture Test (built-in, bypasses module system) ──
 *
 * Exercises the QEMU EDMAC harness by writing directly to EDMAC channel 2
 * MMIO registers.  Runs as a DryOS task spawned from platform_post_init().
 * Waits 8 seconds for full ML boot before starting.
 *
 * Enable QEMU harness with: QEMU_EOS_SYNTHETIC_EDMAC=2
 * Output: ML/LOGS/EDMAC_TEST.RAW, ML/LOGS/EDMAC_TEST.TXT
 */

#define EDMAC_CH2_BASE      0xD0004200
#define EDMAC_FRAME_W       1872
#define EDMAC_FRAME_H       1060
#define EDMAC_BPP           14
#define EDMAC_LINE_BYTES    ((EDMAC_FRAME_W * EDMAC_BPP + 7) / 8)  /* 3276 */

#define EDMAC_REG_CONTROL   0x00
#define EDMAC_REG_FLAGS     0x04
#define EDMAC_REG_ADDR      0x08
#define EDMAC_REG_YN_XN     0x0C
#define EDMAC_REG_YB_XB     0x10
#define EDMAC_REG_YA_XA     0x14
#define EDMAC_REG_OFF1A     0x18
#define EDMAC_REG_OFF1B     0x1C
#define EDMAC_REG_OFF2A     0x20
#define EDMAC_REG_OFF2B     0x24
#define EDMAC_REG_OFF3      0x28
#define EDMAC_REG_IRQ       0x30
#define EDMAC_REG_ABORT     0x34
#define EDMAC_REG_CONN      0x40

static inline void edmac_mmio_write(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(EDMAC_CH2_BASE + off) = val;
}

static inline uint32_t edmac_mmio_read(uint32_t off)
{
    return *(volatile uint32_t *)(EDMAC_CH2_BASE + off);
}

static void edmac_builtin_test_task(void *unused)
{
    (void)unused;
    /* Wait for ML to fully boot */
    printf("[EDMAC_TEST] Task started, waiting 8s for boot...\n");
    msleep(8000);
    printf("[EDMAC_TEST] Woke up from sleep\n");

#ifdef CONFIG_RAW_LIVEVIEW
    qemu_simulate_lv_state();
    qemu_start_vsync_sim();
#endif

    printf("[EDMAC_TEST] === Starting built-in synthetic capture test ===\n");

    /* Write marker file to prove task is running */
    FILE *f = FIO_CreateFile("ML/LOGS/EDMAC_TASK.TXT");
    if (f) {
        FIO_WriteFile(f, "task started\n", 13);
        FIO_CloseFile(f);
    }

    uint32_t buf_size = (uint32_t)EDMAC_LINE_BYTES * EDMAC_FRAME_H;
    printf("[EDMAC_TEST] Frame: %dx%d, %d bpp, %d bytes/line, total %d bytes\n",
                EDMAC_FRAME_W, EDMAC_FRAME_H, EDMAC_BPP, EDMAC_LINE_BYTES, (int)buf_size);

    /* Allocate via fio_malloc (uses shoot memory for DMA-capable large buffers) */
    void *buf = fio_malloc(buf_size + 256);
    if (!buf) {
        printf("[EDMAC_TEST] fio_malloc failed, trying _AllocateMemory\n");
        buf = _AllocateMemory(buf_size + 256);
    }
    if (!buf) {
        printf("[EDMAC_TEST] ERROR: alloc %d bytes failed\n", (int)(buf_size + 256));
        return;
    }

    /* Align to 64 bytes */
    void *aligned = (void *)(((uint32_t)buf + 63) & ~63);
    printf("[EDMAC_TEST] Buffer: raw=0x%08X aligned=0x%08X\n",
                (uint32_t)buf, (uint32_t)aligned);

    /* Zero buffer */
    memset(aligned, 0, buf_size);

    /* Configure EDMAC channel 2 registers */
    edmac_mmio_write(EDMAC_REG_IRQ, 0);
    edmac_mmio_write(EDMAC_REG_ABORT, 0);
    edmac_mmio_write(EDMAC_REG_ADDR, (uint32_t)aligned);
    edmac_mmio_write(EDMAC_REG_YN_XN, ((EDMAC_FRAME_H - 1) << 16) | EDMAC_LINE_BYTES);
    edmac_mmio_write(EDMAC_REG_YB_XB, 0);
    edmac_mmio_write(EDMAC_REG_YA_XA, 0);
    edmac_mmio_write(EDMAC_REG_OFF1A, EDMAC_LINE_BYTES);
    edmac_mmio_write(EDMAC_REG_OFF1B, 0);
    edmac_mmio_write(EDMAC_REG_OFF2A, 0);
    edmac_mmio_write(EDMAC_REG_OFF2B, 0);
    edmac_mmio_write(EDMAC_REG_OFF3, 0);
    edmac_mmio_write(EDMAC_REG_CONN, 0);

    printf("[EDMAC_TEST] Registers configured. Starting transfer...\n");

    /* START — writing 1 to control triggers QEMU harness */
    edmac_mmio_write(EDMAC_REG_CONTROL, 1);

    /* Poll for completion (harness fills synchronously, should be immediate) */
    int timeout = 100;
    uint32_t irq = 0;
    while (timeout > 0) {
        irq = edmac_mmio_read(EDMAC_REG_IRQ);
        if (irq & 0x02) break;
        msleep(10);
        timeout--;
    }

    if (!(irq & 0x02)) {
        printf("[EDMAC_TEST] ERROR: Timeout (IRQ=0x%X). Is QEMU_EOS_SYNTHETIC_EDMAC=2 set?\n", irq);
        return;
    }

    printf("[EDMAC_TEST] Transfer complete! IRQ=0x%02X\n", irq);

    /* Verify buffer */
    uint8_t *data = (uint8_t *)aligned;
    int nonzero = 0;
    for (uint32_t i = 0; i < buf_size; i++) {
        if (data[i] != 0) nonzero++;
    }
    printf("[EDMAC_TEST] Buffer: %d/%d bytes non-zero (%d%%)\n",
                nonzero, (int)buf_size, (int)(buf_size ? nonzero * 100 / (int)buf_size : 0));

    /* Sample top line (colour bars) */
    printf("[EDMAC_TEST] Top: ");
    for (int i = 0; i < 5; i++) {
        uint32_t x = (i * EDMAC_LINE_BYTES) / 5;
        uart_printf("[%d]=0x%02X ", (int)x, data[x]);
    }
    uart_printf("\n");

    /* Sample middle line (gradient) */
    uint32_t mid = (EDMAC_FRAME_H / 2) * EDMAC_LINE_BYTES;
    printf("[EDMAC_TEST] Mid: ");
    for (int i = 0; i < 5; i++) {
        uint32_t x = (i * EDMAC_LINE_BYTES) / 5;
        uart_printf("[%d]=0x%02X ", (int)x, data[mid + x]);
    }
    uart_printf("\n");

    /* Try multiple save paths (QEMU SD card I/O varies) */
    const char *paths[] = {
        "B:/EDMAC_TEST.RAW",
        "A:/EDMAC_TEST.RAW",
        "ML/LOGS/EDMAC_TEST.RAW",
        "EDMAC_TEST.RAW",
        NULL
    };
    int saved = 0;
    for (int p = 0; paths[p] && !saved; p++) {
        printf("[EDMAC_TEST] Trying save: %s\n", paths[p]);
        f = FIO_CreateFile(paths[p]);
        if (f) {
            FIO_WriteFile(f, aligned, buf_size);
            FIO_CloseFile(f);
            printf("[EDMAC_TEST] Saved %d bytes to %s\n", (int)buf_size, paths[p]);
            saved = 1;
        }
    }
    if (!saved) {
        printf("[EDMAC_TEST] WARNING: Could not save to any path (QEMU limitation)\n");
    }

    /* Dump hex sample of first 64 bytes via UART for external verification */
    printf("[EDMAC_TEST] Hex dump (first 64 bytes):\n");
    for (int row = 0; row < 4; row++) {
        uart_printf("  %04X: ", row * 16);
        for (int col = 0; col < 16; col++) {
            uart_printf("%02X ", data[row * 16 + col]);
        }
        uart_printf("\n");
    }

    /* Dump bottom-third sample (checkerboard region) */
    uint32_t bot = ((EDMAC_FRAME_H * 2 / 3) + 4) * EDMAC_LINE_BYTES;
    printf("[EDMAC_TEST] Checkerboard (y=%d, first 64 bytes):\n",
                (int)((EDMAC_FRAME_H * 2 / 3) + 4));
    for (int row = 0; row < 4; row++) {
        uart_printf("  %04X: ", (int)(bot + row * 16));
        for (int col = 0; col < 16; col++) {
            uart_printf("%02X ", data[bot + row * 16 + col]);
        }
        uart_printf("\n");
    }

    printf("[EDMAC_TEST] === TEST COMPLETE ===\n");
}
