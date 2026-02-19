/**
 * Camera internals for 80D 1.0.3
 */

/** This camera has a DIGIC VI chip */
#define CONFIG_DIGIC_VI

/** Digic 6 does not have bitmap font in ROM, try to load it from card **/
#define CONFIG_NO_BFNT

/* has LV */
#define CONFIG_LIVEVIEW

/* display filter buffer swap for anamorphic/defishing preview */
#define CONFIG_DISPLAY_FILTERS

/* enable state objects hooks */
//#define CONFIG_STATE_OBJECT_HOOKS

// SRM is untested, this define is to allowing building
// without SRM_BUFFER_SIZE being found
#define CONFIG_MEMORY_SRM_NOT_WORKING

#define CONFIG_MALLOC_STRUCT_V2

#define CONFIG_TASK_STRUCT_V2
#define CONFIG_TASK_ATTR_STRUCT_V4
#define CONFIG_AUDIO_CONTROLS

/* Pre-initialize RGBA VRAM in QEMU (Canon's GIS never populates _rgb_vram_info
 * in emulation, so boot_post_init_task would block forever on bmp_vram_raw()) */
#define CONFIG_PLATFORM_POST_INIT

/* Mirror ML console printf() to UART so QEMU serial captures module loading */
#define CONFIG_COPY_CONSOLE_TO_UART
