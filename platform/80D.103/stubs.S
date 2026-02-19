/** \file
 * Entry points into the firmware image.
 *
 * These are the functions that we can call from our tasks
 * in the Canon 1.0.3 firmware for the EOS 80D.
 *
 */
/*
 * Copyright (C) 2023 Magic Lantern Team
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

#include <stub.h>

/* Warning: there are some ARM32 stubs, not only thumb! */

/** Startup **/
NSTUB(MAIN_FIRMWARE_ADDR, firmware_entry)
THUMB_FN(0xfe0d314c,  pre_cstart_func) // needed to easily patch for compacting firmware_entry and cstart regions
THUMB_FN(0xfe0d316c,  cstart)
ARM32_FN(0xfe3fc21c,  bzero32)
ARM32_FN(0xfe3fbc78,  create_init_task)
THUMB_FN(0xfe0d3384,  init_task)

/** Tasks **/
//DATA_PTR(  0x44D0,  interrupt_level)
DATA_PTR(    0x44D4,  current_interrupt) // See fe0cd12c() which uses both current_interrupt and interrupt_level.
                                         // It's read from d4011000, this was consistent on 3 Digic 6 cams I checked.
DATA_PTR(    0x44D4,  current_interrupt)
DATA_PTR(    0x44F4,  current_task)
THUMB_FN(     0xED0,  msleep)
THUMB_FN(     0xFF0,  task_create)
DATA_PTR(    0x46a0,  task_max)

/** File I/O **/
THUMB_FN(0xfe4827ba, _FIO_CreateFile)
THUMB_FN(0xfe482744, _FIO_OpenFile)
THUMB_FN(0xfe482a10,  FIO_CloseFile)
THUMB_FN(0xfe482890, _FIO_ReadFile)
THUMB_FN(0xfe4829a2, _FIO_WriteFile)
THUMB_FN(0xfe482900,  FIO_SeekSkipFile)
THUMB_FN(0xfe482afc, _FIO_GetFileSize)
THUMB_FN(0xfe482824, _FIO_RemoveFile)
THUMB_FN(0xfe482e80, _FIO_RenameFile)                       // FIO_RenameFile_Success
THUMB_FN(0xfe482ffe, _FIO_CreateDirectory)
THUMB_FN(0xfe4833c6, _FIO_FindFirstEx)
THUMB_FN(0xfe48345a,  FIO_FindNextEx)
THUMB_FN(0xfe4834dc,  FIO_FindClose)
THUMB_FN(0xfe4830ee,  FIO_Flush)

/** Debug messages **/
THUMB_FN(0x800035E0,  DryosDebugMsg)

/** Eventprocs (call by name) **/
THUMB_FN(0xfe48422e,  call)

/** GUI timers **/
THUMB_FN(0xfe4b3ac2,  SetHPTimerNextTick)                   // same "worker" function as SetHPTimerAfterNow
THUMB_FN(0xfe4b3a72,  SetHPTimerAfterNow)                   // from error message
THUMB_FN(0xfe483fe4,  SetTimerAfter)                        // from error message
THUMB_FN(0xfe48405a,  CancelTimer)                          // from error message

/** Memory allocation **/
THUMB_FN(     0x4D6, _AllocateMemory)
THUMB_FN(     0x670, _FreeMemory)
THUMB_FN(     0x380,  GetMemoryInformation)
THUMB_FN(     0x354,  GetSizeOfMaxRegion)
THUMB_FN(0x800062b8, _alloc_dma_memory)
THUMB_FN(0x800062e4, _free_dma_memory)
THUMB_FN(0xfe60d130, _malloc)                               // called before "can't malloc"
THUMB_FN(0xfe60d166, _free)                                 // called later on the same buffer

/** Memory info **/
THUMB_FN(0xFE2DB8AC,  malloc_info)
THUMB_FN(0xfe2ab650,  memmap_info)                          // Exception vector, DRYOS system memory etc
THUMB_FN(0xFE15F746,  smemShowFix)
THUMB_FN(0xFE2DB944,  sysmem_info)

/** Misc **/
THUMB_FN(0xfe483d00,  uart_printf)
THUMB_FN(0x800054AC,  vsnprintf)

/** New stubs **/

/** Startup **/
DATA_PTR(    0x76bd,  additional_version)
/* kitor: Please note that this points at '\n' char in char* with FW version */

/** Tasks **/
DATA_PTR(    0x46dc,  task_dispatch_hook)
THUMB_FN(0x8000172c,  task_trampoline)
THUMB_FN(0x8000135c, _get_task_info_by_id)

/** Recursive locks **/
THUMB_FN(    0x267a,  CreateRecursiveLock)                  // "KernelDry/KerMisc.c"
THUMB_FN(    0x2900,  AcquireRecursiveLock)                 // "KernelDry/KerRLock.c", used in WINSYS_CreateController()
THUMB_FN(    0x29c8,  ReleaseRecursiveLock)                 // "KernelDry/KerRLock.c"

/** Semaphores **/
THUMB_FN(    0x23e0,  create_named_semaphore)
THUMB_FN(    0x24d6,  take_semaphore)                       // GetEndGuiInitState Take/Give
THUMB_FN(    0x24a6,  take_semaphore_now)
THUMB_FN(    0x254e,  give_semaphore)

/** GUI **/
DATA_PTR(    0xc190,  gui_task_list)                        // via WINSYS_CreateController()
DATA_PTR(    0x4b24,  gui_main_struct)                      // structure used by gui_main_task
THUMB_FN(0xfe466592,  SetGUIRequestMode)                    // by debug message
THUMB_FN(0xfe18a2cc,  gui_main_task)                        // task_create("GuiMainTask"...
THUMB_FN(0xfe30cf00,  gui_massive_event_loop)               // various "GUICMD_"* strings
THUMB_FN(0xfe18a264,  gui_enqueue_message)                  // via string "warning! QueueLength="
THUMB_FN(0xfe47a734,  gui_init_end)                         // EndGuiInit
THUMB_FN(0xfe18a472,  GUI_Control)                          // string GUI_Control

/** Dialog API **/
THUMB_FN(0xfe490fdc,  dialog_redraw)                        // DebugMsg "StartPlayErrorApp". Called just before assigning 0 to one of variables.
THUMB_FN(0xfe4847d0,  dialog_set_property_str)              // DebugMsg "***** UpdateStroboZoomGuidance", called with char* buffer as 3rd param.

/** GUI timers **/
THUMB_FN(0xfe651f44,  CancelDateTimer)                      // by debug message

/** Queue **/
THUMB_FN(    0x2120,  msg_queue_create)                     // easy to spot in create_gui_main_task aka GUI_initialize
THUMB_FN(    0x21e0,  msg_queue_receive)                    // used on gui queue in gui_main_task
THUMB_FN(    0x2334,  msg_queue_post)                       // used in gui_enqueue_message
THUMB_FN(    0x236a,  msg_queue_count)                      // educated guess based on R/200D. Also xref from fn with "[TASK ERROR] GetNumOfEvent"

/** Memory **/
// SRM general
THUMB_FN(0xfe583e66,  AllocateMemoryResource)               // Assert m_pfAllocMemoryCBR
THUMB_FN(0xfe583e94,  AllocateContinuousMemoryResource)     // Assert m_pfContAllocMemoryCBR
THUMB_FN(0xfe583f3a,  FreeMemoryResource)                   // Assert m_pfFreeMemoryCBR
// SRM specific
THUMB_FN(0xfe45fef2,  SRM_AllocateMemoryResourceFor1stJob)  // via debug message with function name (after call). Function uses RscMgr strings
THUMB_FN(0xfe46233a,  SRM_FreeMemoryResourceFor1stJob)      // used in functions related to deleting Skelton jobs
// MemorySuite, MemoryChunk
THUMB_FN(0xfe477e3a,  ConcatMemorySuite)                    // Via debug msg with fn name (after call). Not used by ML, but utilizes functions below:
THUMB_FN(0xfe477578,  GetFirstChunkFromSuite)               // In ConcatMemorySuite(), First function called.
THUMB_FN(0xfe477df4,  GetNextMemoryChunk)                   // In ConcatMemorySuite(), function called in first loop with param2 passed as 1st arg
THUMB_FN(0xfe477378,  GetMemoryAddressOfMemoryChunk)        // In ConcatMemorySuite(), uses param2 from GetNextMemoryChunk() as own param1
THUMB_FN(0xfe476f26,  CreateMemoryChunk)                    // In ConcatMemorySuite(), just after calls to GetMemoryAddressOfMemoryChunk(); also called from CreateMemorySuite()
THUMB_FN(0xfe4774ec,  AddMemoryChunk)                       // In ConcatMemorySuite(), just after calls to CreateMemoryChunk()
THUMB_FN(0xfe4779ee,  CreateMemorySuite)                    // One of just a few xrefs of CreateMemoryChunk(); String "PackMem.c", calls malloc(0x18)
THUMB_FN(0xfe477708,  DeleteMemorySuite)                    // DebugMsg "DeleteMemorySuite 0x%x"

/** Uncategorized **/
DATA_PTR(    0xcca0,  sd_device)                            // From function that references "SD_DeviceCreate:" TODO: check if stryct changed like in R
THUMB_FN(0xfe620c5e,  _LoadCalendarFromRTC)                 // "Private" as it now has more arguments, needs a wrapper.
THUMB_FN(0xfe6a1412,  ErrForCamera_handler)                 // ERR70, ERR80 etc (DlgErrForCamera.c)

/** Properties **/
THUMB_FN(0xfe484c0a,  prop_register_slave)                  // Assert "pRegisterSlave"
THUMB_FN(0xfe484cda, _prop_request_change)                  // Assert "pRequestChange"; 2nd (almost identical) is at 0xfe44574a but uses different struct address - matched to struct from prop_register_slave()
THUMB_FN(0xfe4851ec, _prop_cleanup)                         // DebugMsg "ChangeCBR(...". Last call in those functions.

/** CFn **/
THUMB_FN(0xfe37115a, GetCFnData)                            // "GetCFnData", like 200D
THUMB_FN(0xfe37128a, SetCFnData)                            // "SetCFnData"

/**
 * RGBA / Compositor functions and structures
 */

/** General FEATURE_VRAM_RGBA stubs **/
// XimrExe overridden in function_overrides.c (QEMU: XIMR HW not emulated, Canon stub crashes)
//THUMB_FN(0xfe22b2fc, XimrExe);                              //In RefreshVrmsSurface, just before "XimrExe" debug message
DATA_PTR(    0xc28c, _rgb_vram_info);
DATA_PTR(    0xc288, winsys_sem);                           // Used in RefreshVrmsSurface around XimrExe call

/** Wrong on purpose **/
DATA_PTR(       0x0,  LCD_Palette)                          // D6 has no indexed RGB buffers.


/** App handlers **/
/**
 * Those can usually be found by looking at function address passed to
 * WINSYS_CreateDialogBox_wrapper / WINSYS_CreateDialogBox, just after some
 * debug message */
THUMB_FN(0xfe41b9ca,  PlayMain_handler)                     // msg: "StartPlayMain"
THUMB_FN(0xfe65071a,  ShootOlcApp_handler)                  // msg: "StartShootOlcApp PushPalette(%d)"
THUMB_FN(0xfe391998,  LiveViewApp_handler)                  // msg: "StartLiveViewApp(%#x)"
//THUMB_FN(0xfe72190d,  LiveViewShutterApp_handler)         // msg: "LiveViewShutterApp"
THUMB_FN(0xfe32f154,  PlayMovieGuideApp_handler)            // msg: "StartPlayMovieGuideApp"

// See R180 stubs.s for details.
//DATA_PTR(  0xb7c0,  PlayMain_dialog)                      // in StartPlayMain(), 0x2bd54 + 0xc
//DATA_PTR(  0xe75c,  ShootOlcApp_dialog)                   // in StartShootOlcApp()
DATA_PTR(    0xe4a0,  LiveViewApp_dialog)                   // in StartLiveViewApp()
//DATA_PTR( 0x11b88,  LiveViewShutterApp_dialog)            // in StartLiveViewShutterApp(), not sure if needed
//DATA_PTR(  0xf1e4,  PlayMovieGuideApp_dialog)             // in StartPlayMovieGuideApp()

/**
 * EDMAC — Engine DMA Controller
 *
 * --- ARCHITECTURE (confirmed from ROM1.BIN analysis, 2026-02-18) ---
 *
 * DIGIC 6 (80D) does NOT use 0xD0000000 + N×0x200 stride as assumed.
 * Instead, Canon uses a lookup TABLE at 0xfeb53e30 (48 entries × 12 bytes each):
 *   entry[i] = { type_field (u32), mmio_addr (u32), handler_fn (u32) }
 * Channel MMIO addresses use 0x100 stride within each bank:
 *   Bank 0: 0xD0004000–0xD0004600 (channels 0–6, stride 0x100)
 *   Bank 1: 0xD0026000–... (channels 7–13)
 *   Bank 2: 0xD0030000–... (channels 14–24)
 *   Bank 3: 0xD0057000–... (channels 25–36, handler fn@0xfe338ddb)
 *   Bank 4: 0xD0058000–... (channels 37+)
 * RAW_LV_EDMAC_CHANNEL_ADDR = 0xD0004200 = table entry[2].mmio_addr → Canon channel 2.
 * shamem_read(0xD0004200 + 0x08) returns the DMA buffer pointer written by ConnectWriteEDmac.
 *
 * --- MMIO HELPER FUNCTIONS ---
 * fn@0xfe420caa — shadowed MMIO write(addr, value): writes to shadow array + real MMIO
 * fn@0xfe420cd8 — shadow read(addr): reads from shadow array (mirror of MMIO state)
 * fn@0xfe420ce4 — locked RMW(addr, AND_mask, OR_value): atomic read-modify-write
 * fn@0xfe420d0a — bulk MMIO write(mmio_base, *src_array, n_words): shadow+real, n_words words
 *
 * --- EDMAC FUNCTION CLUSTER (0xfe338136–0xfe3384ca) found via "Edmac/Edmac.c" string ---
 * The cluster was located by finding the assert string "Edmac/Edmac.c" at 0xfe338511 and
 * tracing back through its xrefs to the function block that precedes it.
 *
 * --- CALLBACK TABLE LAYOUT ---
 * Completion ISR type 1 (ch 0–24, handler fn@0xfe338d09):
 *   Reads callback fn_ptr from 0x424b8 + ch×4 (type_1_cbr_table, 4B/entry)
 *   Reads callback + ctx from 0x42578 + ch×12 and calls fn(ctx) after MMIO ops
 * Completion ISR type 2 (ch 25+, handler fn@0xfe338ddb):
 *   Reads callback fn from 0x42578 + ch×12, ctx from +4, calls fn(ctx)
 * RegisterEDmacCompleteCBR write path: NOT FOUND in ROM scan — likely written during
 *   Canon's EDMAC init or accessed via a different indirection.  Keep as no-op stub.
 *
 * --- PHASE 2 FALSE POSITIVES (FindEDMAC_D6.py) ---
 * Capstone found 0 BL/BLX calls to _EngDrvOut (0xfe15aec0) in 32 MB ROM.
 * Canon does NOT call _EngDrvOut internally — it is an ML MMIO thunk only.
 * Phase 2 "EDMAC" candidates (0xFE0Axxx) arose from code bytes at aligned boundaries
 * accidentally matching the 0xD000xxxx 4-byte pattern when read as data — scanner flaw.
 *
 * --- CONFIDENCE RATINGS ---
 *   HIGH          = verified correct (disassembly matches expected instructions)
 *   POSSIBLE      = plausible candidate, not confirmed on hardware
 *   FALSE_POSITIVE = disassembly proved wrong function type
 */

/**
 * EDMAC cluster — all addresses confirmed by disassembly of ROM1.BIN 1.0.3
 *
 * SIGNATURE NOTES for wrappers in function_overrides.c:
 *
 * StartEDmac:
 *   Canon fn @ 0xfe338136: takes (channel) only; clears state table[ch], RMW mmio+4
 *   to set bit 29, DSB barrier, then writes 1 to mmio+0 via shadow-write.
 *   ML API:   StartEDmac(channel, flags) — extra `flags` arg ignored in wrapper.
 *
 * SetEDmac:
 *   Canon fn @ 0xfe3382f2: takes (channel, &edmac_info); validates state, processes
 *   11 fields from edmac_info (offsets 0x00–0x28) with masking, bulk-writes to mmio+0xc.
 *   Canon fn @ 0xfe338236: variant with read-first (reads current MMIO state first).
 *   ML API:   SetEDmac(channel, addr, &edmac_info, flags) — addr and flags ignored;
 *   the address is set separately by ConnectWriteEDmac on DIGIC 6.  Wrapper calls
 *   Canon's fn with (channel, &edmac_info) only.
 *
 * ConnectWriteEDmac:
 *   Canon fn @ 0xfe33820c: takes (channel, buf_addr); strips top 2 bits of buf_addr
 *   (bic r1, r5, #0xc0000000), writes result to mmio+8 via shadow-write.  COMPATIBLE
 *   with ML's API: `where` in ConnectWriteEDmac(channel, where) IS the DMA buffer
 *   address on DIGIC 6 (not a connection ID).  No wrapper needed.
 *
 * AbortEDmac:
 *   Canon fn @ 0xfe338176: takes (channel); sets state[ch]=1, RMW(mmio+4, ~7, 6)
 *   to write abort pattern (bits 1|2) to control register.  ML API matches (1 arg).
 *
 * RegisterEDmacCompleteCBR:
 *   Write path to 0x424b8 / 0x42578 callback tables NOT found in ROM scan.
 *   fn @ 0xfe338196 writes r1 to 0x424b8+ch×4 AND to mmio+4 bits 0-2 — may be
 *   a mode-setting or abort-CBR function, NOT RegisterEDmacCompleteCBR.
 *   Keep as no-op until confirmed on hardware.  Without this, mlv_lite will not
 *   receive DMA-complete notifications and frames will not be processed.
 */

/** FALSE POSITIVES from FindEDMAC_D6.py Phase 2 — kept for historical reference **/
//THUMB_FN(0xfe0af2f7, SetEDmac                           ) // [FALSE_POSITIVE]  timing/counter fn
//THUMB_FN(0xfe0b2221, ConnectWriteEDmac                  ) // [FALSE_POSITIVE]  state-machine/event fn
//THUMB_FN(0xfe0bcaf3, ConnectReadEDmac                   ) // [FALSE_POSITIVE]  polling/status fn
//THUMB_FN(0xfe0bdff9, StartEDmac                         ) // [FALSE_POSITIVE]  complex state machine
//THUMB_FN(0xfe0c3033, AbortEDmac                         ) // [FALSE_POSITIVE]  unconfirmed cluster
//THUMB_FN(0xfe0c8937, RegisterEDmacCompleteCBR           ) // [FALSE_POSITIVE]  event-dispatch wrapper (ldrb.w [r0,#0x10d])
//THUMB_FN(0xfe0c8c8f, UnregisterEDmacCompleteCBR         ) // [FALSE_POSITIVE]  event-dispatch wrapper
//THUMB_FN(0xfe0c93c5, RegisterEDmacAbortCBR              ) // [FALSE_POSITIVE]  event-dispatch wrapper
//THUMB_FN(0xfe0c93dd, UnregisterEDmacAbortCBR            ) // [FALSE_POSITIVE]  event-dispatch wrapper
//THUMB_FN(0xfe0c93f5, RegisterEDmacPopCBR                ) // [FALSE_POSITIVE]  event-dispatch wrapper
//THUMB_FN(0xfe0c940d, UnregisterEDmacPopCBR              ) // [FALSE_POSITIVE]  event-dispatch wrapper

/** EDMAC cluster — POSSIBLE candidates from Capstone analysis of ROM1.BIN **/
/** Enable after hardware confirmation. Wrappers in function_overrides.c handle sig mismatches. **/
//THUMB_FN(0xfe338136, StartEDmac                         ) // [POSSIBLE] 1-arg wrapper; cmp r0,#0x19; push; writes 1→mmio+0 with DSB
//THUMB_FN(0xfe338176, AbortEDmac                         ) // [POSSIBLE] 1-arg; state[ch]=1; RMW(mmio+4,~7,6) — abort bits
//THUMB_FN(0xfe33820c, ConnectWriteEDmac                  ) // [POSSIBLE] (ch,buf); bic #0xc0000000; writes to mmio+8
//THUMB_FN(0xfe3382f2, SetEDmac                           ) // [POSSIBLE] (ch,&info); 11-field bulk write to mmio+0xc — needs 4→2 arg wrapper
//THUMB_FN(0xfe338236, SetEDmac                           ) // [POSSIBLE] variant: read-first before write (use 0xfe3382f2 as primary)

/** fn@0xfe338196: stores r1 to 0x424b8+ch×4 AND writes r1 to mmio+4 bits 0–2.
 *  Possible RegisterEDmacAbortCBR or channel-mode setter — NOT RegisterEDmacCompleteCBR.
 *  //THUMB_FN(0xfe338196, ???) **/

/** Shared memory / MMIO helpers **/
// Adjacent 2-instruction pair found by capstone scan of real ROM1.BIN (fw 1.0.2).
// _EngDrvOut: str r1, [r0]; bx lr  — writes r1 (value) to r0 (reg addr)
// shamem_read: ldr r0, [r0]; bx lr — dereferences r0, returns u32
// Canon groups these paired MMIO utilities adjacently; HIGH confidence.
THUMB_FN(0xfe15aec1, _EngDrvOut                         ) // [HIGH]   verified: str r1,[r0]; bx lr — 2-insn MMIO write
THUMB_FN(0xfe15aec5, shamem_read                        ) // [HIGH]   verified: ldr r0,[r0]; bx lr — 2-insn MMIO read, adjacent to EngDrvOut

// Superseded candidates (wrong addresses — kept for reference):
//THUMB_FN(0xfe6e40d3, shamem_read                        ) // [WRONG]  FindEDMAC_D6 Phase 3 false positive — 40-insn fn, not tiny
//THUMB_FN(0xfe26c459, _EngDrvOut                         ) // [WRONG]  FindEDMAC_D6 Phase 4 off-by-one — garbage disassembly

/** Resource lock **/
// CreateResLockEntry near 0xFE980000 — two candidates identified 2026-02-18:
//
// fn @ 0xFE98004D (~60B, small wrapper):
//   push {r4, r5, lr}
//   Loads global table ptr from [pc] → 0xfe980154 = 0x01AC2394
//   Lazily inits resource-lock slots [r4] and [r4+8]
//   Tail-calls 0xFE9BA84C (ARM32 entry: starts with 'bx pc' → mode switch to ARM)
//   Likely a thin wrapper over the real allocator.
//
// fn @ 0xFE98008F (~218B, 4-arg, PREFERRED CANDIDATE):
//   push.w {r4, r5, r6, r7, r8, sb, sl, lr}
//   sub sp, #0xb8 (184-byte stack frame)
//   Takes 4+ arguments (matching Canon's internal CreateResLockEntry signature)
//   Contains assert handler call → 'RscLckEntry1st != NULL' asserts nearby
//   Strong match for CreateResLockEntry by size/arg-count/surrounding strings.
//
// 5D4.133 CreateResLockEntry @ 0xFE9FD4A8 disassembles mid-function in 80D ROM
// → code layout differs between 5D4 and 80D; can't cross-port address directly.
//
// TODO: boot with ml debug menu → "Dump ROM and RAM" → run find_edmac_table.py to confirm.
//
//THUMB_FN(0xfe98004d, CreateResLockEntry                 ) // [POSSIBLE] thin wrapper (tail-calls ARM32 allocator)
//THUMB_FN(0xfe98008f, CreateResLockEntry                 ) // [POSSIBLE] 4-arg 218B fn — preferred candidate
//THUMB_FN(0xfe985dc5, CreateResLockEntry                 ) // [FALSE_POSITIVE]  data region, not code
