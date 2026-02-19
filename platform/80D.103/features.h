#define FEATURE_VRAM_RGBA

// Don't Click Me menu looks to be intended as a place
// for devs to put custom code in debug.c run_test(),
// and allowing triggering from a menu context.
#define FEATURE_DONT_CLICK_ME

#define FEATURE_SHOW_SHUTTER_COUNT

// working but incomplete, some allocators don't report
// anything yet as they're faked / not yet found
#define FEATURE_SHOW_FREE_MEMORY

#define CONFIG_ADDITIONAL_VERSION
#define FEATURE_SCREENSHOT

#define CONFIG_TSKMON
#define FEATURE_SHOW_TASKS
#define FEATURE_SHOW_CPU_USAGE
#define FEATURE_SHOW_GUI_EVENTS

// enable global draw
#define FEATURE_GLOBAL_DRAW
#define FEATURE_CROPMARKS

//#define CONFIG_PROP_REQUEST_CHANGE
//#define CONFIG_STATE_OBJECT_HOOKS
#define CONFIG_LIVEVIEW
#define FEATURE_POWERSAVE_LIVEVIEW

// We can't yet rely on image capture.  Cam crashes due to null pointer,
// I think?  If it fails to AF lock, for example.
// #define CONFIG_IMAGE_CAPTURE_NOT_WORKING  /* focus stacking: AF_DONT_CHANGE path is safe; lens_focus is property-based */

#define FEATURE_PICSTYLE
#define CONFIG_PROP_REQUEST_CHANGE

// explicitly disable stuff that don't work or may break things
#undef CONFIG_STATE_OBJECT_HOOKS
#undef CONFIG_CRASH_LOG
#undef CONFIG_AUTOBACKUP_ROM

// Focus stacking — enabled because:
// - CONFIG_PROP_REQUEST_CHANGE is defined (lens_focus uses prop_request_change_wait)
// - capture chain uses call("Release") @ 0xfe48422e (already stubbed)
// - AF_DONT_CHANGE path skips lens_setup_af → no null pointer crash
// - lens_focus is entirely property-based (PROP_LV_LENS_DRIVE_REMOTE)
#define FEATURE_FOCUS_STACKING

// Raw LiveView features — enabled by CONFIG_RAW_LIVEVIEW in internals.h
// all_features.h auto-enables FEATURE_RAW_HISTOGRAM and FEATURE_RAW_SPOTMETER
// from CONFIG_RAW_LIVEVIEW; additional raw overlays enabled here:
// FEATURE_RAW_ZEBRAS: disabled for now — zebra.c forward decl issue with DIGIC 6
// FEATURE_RAW_FOCUS_PEAKING: needs focus_peaking.c integration
// #define FEATURE_RAW_ZEBRAS
// #define FEATURE_RAW_FOCUS_PEAKING

// Raw photo overlays (review mode)
#define CONFIG_RAW_PHOTO

// FPS override — auto-enabled by all_features.h (FEATURE_FPS_OVERRIDE, FEATURE_FPS_RAMPING)
// Required stubs: all present (shamem_read, SetHPTimerAfterNow, SetTimerAfter, CancelTimer)
// FPS registers: defined in fps-engio_per_cam.h (0xD0006008/0xD0006014/0xD0006000)
// Status: compiles clean, needs hardware verification

// CMOS/ADTG register writes — NOT available on any DIGIC 6 camera
// Blocks: dual_iso, crop_rec
// Requires: ROM1.BIN dump → Ghidra search for "CMOS[%d]write" / "ADTG_WRITE"
