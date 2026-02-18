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

/** WRONG: temporary overrides to get CONFIG_HELLO_WORLD working **/

void SetEDmac(unsigned int channel, void *address, struct edmac_info *ptr, int flags)
{
    return;
}

void ConnectWriteEDmac(unsigned int channel, unsigned int where)
{
    return;
}

void ConnectReadEDmac(unsigned int channel, unsigned int where)
{
    return;
}

void StartEDmac(unsigned int channel, int flags)
{
    return;
}

void AbortEDmac(unsigned int channel)
{
    return;
}

void RegisterEDmacCompleteCBR(int channel, void (*cbr)(void*), void* cbr_ctx)
{
    return;
}

void UnregisterEDmacCompleteCBR(int channel)
{
    return;
}

void RegisterEDmacAbortCBR(int channel, void (*cbr)(void*), void* cbr_ctx)
{
    return;
}

void UnregisterEDmacAbortCBR(int channel)
{
    return;
}

void RegisterEDmacPopCBR(int channel, void (*cbr)(void*), void* cbr_ctx)
{
    return;
}

void UnregisterEDmacPopCBR(int channel)
{
    return;
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
