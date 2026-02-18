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
