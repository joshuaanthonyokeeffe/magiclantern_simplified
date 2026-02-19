MODEL = 80D
FW_VERSION = 103

TOP_DIR = ../..

AUTOEXEC_BASE = 0x800000
FIR_BASE = 0x800120

#CANON_FIR  = 80D00103.FIR
FIRMWARE_ID     = 0x80000350
#UPDATE_NAME_FIR = BOOT_80D.FIR
MAIN_FIRMWARE_ADDR = 0xFE0A0000

# user_mem_start from cstart struct. Align to 0x10 up
RESTARTSTART = 0x1cc400

# Cortex R4
CFLAGS_PLATFORM += -mthumb -mlong-calls
PLATFORM_ARCH = armv7-r

ML_BOOT_OBJ = boot-d678.o
ML_SRC_EXTRA_OBJS += $(BUILD_DIR)/function_overrides.o

# When building with ROM_DUMP_ONLY, add the flag and the extra object.
# Must be before include ../Makefile so CFLAGS_PLATFORM and ML_SRC_EXTRA_OBJS are visible.
ifdef CONFIG_ROM_DUMP_ONLY
CFLAGS_PLATFORM += -DCONFIG_ROM_DUMP_ONLY
ML_SRC_EXTRA_OBJS += $(BUILD_DIR)/rom0_dump.o
endif

include ../Makefile
.DEFAULT_GOAL := $(BUILD_DIR)/cam_complete

# Standalone ROM dumper: make rom_dump
# Builds autoexec.bin that only dumps ROM to SD card, no ML init.
# Runs a clean sub-make with CONFIG_ROM_DUMP_ONLY=y, then copies the result.
ROM_DUMP_DIR = $(BUILD_DIR)/rom_dump
rom_dump:
	$(MAKE) clean
	$(MAKE) CONFIG_ROM_DUMP_ONLY=y
	@mkdir -p $(ROM_DUMP_DIR)
	@cp $(BUILD_DIR)/autoexec.bin $(ROM_DUMP_DIR)/autoexec.bin
	@echo "ROM dumper built: $(ROM_DUMP_DIR)/autoexec.bin"
	@echo "Deploy: copy $(ROM_DUMP_DIR)/autoexec.bin to SD card root"
