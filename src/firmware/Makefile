CROSS=riscv-none-elf-
AR=$(CROSS)ar

# ─── Artifact identity ──────────────────────────────────────────────────────
TARGET  = nodenet_riscv
VERSION = 0.1.0
# Git hash appended to version string (adds -dirty suffix on uncommitted changes)
GIT_HASH := $(shell git describe --always --dirty 2>/dev/null || echo unknown)

# Entry-point source selection:
# - Default build uses main.cpp
# - Bring-up/test build uses test_main.cpp
# You can still override manually: make MAIN_SRC=test_main.cpp
MAIN_SRC ?= main.cpp
MAIN_OBJ = $(BUILD)/$(basename $(notdir $(MAIN_SRC))).o
BOOT_SRC = boot_stage0.cpp
BOOT_OBJ = $(BUILD)/$(basename $(notdir $(BOOT_SRC))).o

PYTHON ?= python
IMG_TOOL = tools/pack_firmware.py
APP_LOAD_ADDR ?= 0x20000000
APP_ENTRY_ADDR ?= 0x20000000

# Persistent cache root for third-party library objects/archives.
# Reuse this pattern if more vendored libraries need expensive rebuilds later.
CACHE_ROOT=.cache

# u8g2 library location
U8G2_DIR=lib/u8g2/csrc
U8G2_SRCS=$(wildcard $(U8G2_DIR)/*.c)
U8G2_CACHE=$(CACHE_ROOT)/u8g2
U8G2_LIB=$(CACHE_ROOT)/libu8g2.a
U8G2_OBJS=$(patsubst $(U8G2_DIR)/%.c,$(U8G2_CACHE)/%.o,$(U8G2_SRCS))

# Bare-metal I2C helper library
I2C_DIR=lib/i2c
I2C_SRC=$(I2C_DIR)/i2c.cpp
I2C_OBJ=$(BUILD)/i2c.o
SERIAL_DIR=lib/serial
SERIAL_SRC=$(SERIAL_DIR)/Serial.cpp
SERIAL_OBJ=$(BUILD)/Serial.o
FLASH_DIR=lib/flash
FLASH_SRC=$(FLASH_DIR)/flash.cpp
FLASH_OBJ=$(BUILD)/flash.o
FLASHDB_DIR=lib/FlashDB-2.2.0
FLASHDB_INC=$(FLASHDB_DIR)/inc
FLASHDB_SRC_DIR=$(FLASHDB_DIR)/src
FLASHDB_PORT_SRC=$(FLASH_DIR)/flashdb_port.cpp
FLASHDB_PORT_OBJ=$(BUILD)/flashdb_port.o
FLASHDB_CORE_SRCS=$(FLASHDB_SRC_DIR)/fdb.c $(FLASHDB_SRC_DIR)/fdb_kvdb.c $(FLASHDB_SRC_DIR)/fdb_utils.c
FLASHDB_CORE_OBJS=$(BUILD)/fdb.o $(BUILD)/fdb_kvdb.o $(BUILD)/fdb_utils.o
SDRAM_TEST_SRC=sdram.cpp
SDRAM_TEST_OBJ=$(BUILD)/sdram.o

DEPS=$(BUILD)/start.d $(MAIN_OBJ:.o=.d) $(BUILD)/u8g2_hal.d $(BUILD)/memoryLab.d $(BUILD)/i2c.d $(BUILD)/Serial.d $(BUILD)/flash.d $(BUILD)/flashdb_port.d $(BUILD)/sdram.d $(BUILD)/fdb.d $(BUILD)/fdb_kvdb.d $(BUILD)/fdb_utils.d $(U8G2_OBJS:.o=.d)

CFLAGS=-march=rv32im -mabi=ilp32 \
       -Wall \
       -Os \
       -ffreestanding \
       -ffunction-sections \
       -fdata-sections \
        -MMD \
        -MP \
       -I$(U8G2_DIR) \
       -I$(I2C_DIR) \
	-I$(FLASH_DIR) \
        -I$(SERIAL_DIR) \
        -I$(FLASHDB_INC) \
       -Iinclude \
       -DFIRMWARE_VERSION='"$(VERSION)-$(GIT_HASH)"'

# C++ flags: no exceptions, no RTTI (keeps code small on bare-metal)
# -I.            : src/firmware/ on include path (for i2c.h from lib/u8g2_hal/)
# -Ilib/u8g2_hal : exposes u8g2_hal.h to main.cpp
CXXFLAGS=$(CFLAGS) \
         -std=c++17 \
         -fno-exceptions \
         -fno-rtti \
         -fno-threadsafe-statics \
         -fno-use-cxa-atexit \
         -I. \
         -Ilib/u8g2_hal

# -nostartfiles: don't use toolchain crt0 (we have start.S)
# --specs=nano.specs: newlib-nano provides memcpy/strlen/memset etc.
# --specs=nosys.specs: stub syscalls (_sbrk, _write — not needed bare-metal)
# --gc-sections: discard unused code/data (critical for u8g2 → small binary)
LDFLAGS=-march=rv32im -mabi=ilp32 -T link.ld \
        -nostartfiles \
        -Wl,--gc-sections \
        --specs=nano.specs \
        --specs=nosys.specs

BUILD=build
ROM_CAPACITY_BYTES ?= 65536

FW_BIN = $(BUILD)/$(TARGET).bin
FW_IMG = $(BUILD)/$(TARGET).img
BOOT_TARGET = boot_stage0
BOOT_ELF = $(BUILD)/$(BOOT_TARGET).elf
BOOT_HEX = $(BUILD)/$(BOOT_TARGET).hex

all: $(BUILD)/$(TARGET).hex firmware-stats

.PHONY: bootloader-build firmware-image

.PHONY: firmware-stats

$(BUILD):
	mkdir -p $(BUILD)

$(U8G2_CACHE):
	mkdir -p $@

$(BUILD)/$(TARGET).elf: $(BUILD)/start.o $(MAIN_OBJ) $(BUILD)/u8g2_hal.o $(I2C_OBJ) $(SERIAL_OBJ) $(FLASH_OBJ) $(FLASHDB_PORT_OBJ) $(FLASHDB_CORE_OBJS) $(SDRAM_TEST_OBJ) $(U8G2_LIB) | $(BUILD)
	$(CROSS)g++ $(LDFLAGS) $^ -o $@

$(BUILD)/start.o: start.S | $(BUILD)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(MAIN_OBJ): $(MAIN_SRC) | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

$(BUILD)/u8g2_hal.o: lib/u8g2_hal/u8g2_hal.cpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

$(I2C_OBJ): $(I2C_SRC) | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

$(SERIAL_OBJ): $(SERIAL_SRC) | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

$(FLASH_OBJ): $(FLASH_SRC) | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

$(FLASHDB_PORT_OBJ): $(FLASHDB_PORT_SRC) | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

$(BUILD)/fdb.o: $(FLASHDB_SRC_DIR)/fdb.c | $(BUILD)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(BUILD)/fdb_kvdb.o: $(FLASHDB_SRC_DIR)/fdb_kvdb.c | $(BUILD)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(BUILD)/fdb_utils.o: $(FLASHDB_SRC_DIR)/fdb_utils.c | $(BUILD)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(SDRAM_TEST_OBJ): $(SDRAM_TEST_SRC) | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

# u8g2 C sources are cached outside $(BUILD) so a normal clean does not force
# recompiling the whole graphics library on the next firmware rebuild.
$(U8G2_CACHE)/%.o: $(U8G2_DIR)/%.c | $(U8G2_CACHE)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(U8G2_LIB): $(U8G2_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(CROSS)objcopy \
		-O verilog \
		$< \
		$@

$(FW_BIN): $(BUILD)/$(TARGET).elf
	$(CROSS)objcopy -O binary $< $@

$(FW_IMG): $(FW_BIN) $(IMG_TOOL)
	$(PYTHON) $(IMG_TOOL) \
		--input $(FW_BIN) \
		--output $(FW_IMG) \
		--load-addr $(APP_LOAD_ADDR) \
		--entry-addr $(APP_ENTRY_ADDR)

firmware-image: $(FW_IMG)

$(BOOT_OBJ): $(BOOT_SRC) | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

$(BOOT_ELF): $(BUILD)/start.o $(BOOT_OBJ) $(FLASH_OBJ) | $(BUILD)
	$(CROSS)g++ $(LDFLAGS) $^ -o $@

$(BOOT_HEX): $(BOOT_ELF)
	$(CROSS)objcopy -O verilog $< $@

bootloader-build: $(BOOT_HEX)
	@set -- $$($(CROSS)size $(BOOT_ELF) | tail -n 1); \
	text=$$1; data=$$2; bss=$$3; \
	echo "[BOOT] ELF text/data/bss: $$text/$$data/$$bss bytes"; \
	hex_used_bytes=$$(awk 'BEGIN{addr=0;max=0} {for(i=1;i<=NF;i++){tok=toupper($$i); if(tok ~ /^@/){addr=strtonum("0x" substr(tok,2));} else {addr++; if(addr>max) max=addr;}}} END{print max+0}' $(BOOT_HEX)); \
	rom_cap=$(ROM_CAPACITY_BYTES); \
	rom_pct=$$(awk "BEGIN { printf \"%.2f\", ($$hex_used_bytes*100)/$$rom_cap }"); \
	echo "[BOOT] ROM usage: $$hex_used_bytes / $$rom_cap bytes ($$rom_pct%)"; \
	if [ $$hex_used_bytes -gt $$rom_cap ]; then \
		echo "[BOOT][ERROR] Bootloader payload exceeds wb_rom capacity by $$((hex_used_bytes-rom_cap)) bytes"; \
		exit 2; \
	fi

firmware-stats: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex
	@set -- $$($(CROSS)size $(BUILD)/$(TARGET).elf | tail -n 1); \
	text=$$1; data=$$2; bss=$$3; \
	ram_used=$$((data + bss)); ram_total=65536; \
	ram_pct=$$(awk "BEGIN { printf \"%.2f\", ($$ram_used*100)/$$ram_total }"); \
	hex_file_bytes=$$(wc -c < $(BUILD)/$(TARGET).hex); \
	hex_used_bytes=$$(awk 'BEGIN{addr=0;max=0} {for(i=1;i<=NF;i++){tok=toupper($$i); if(tok ~ /^@/){addr=strtonum("0x" substr(tok,2));} else {addr++; if(addr>max) max=addr;}}} END{print max+0}' $(BUILD)/$(TARGET).hex); \
	rom_cap=$(ROM_CAPACITY_BYTES); \
	rom_pct=$$(awk "BEGIN { printf \"%.2f\", ($$hex_used_bytes*100)/$$rom_cap }"); \
	echo "[FW] ELF text/data/bss: $$text/$$data/$$bss bytes"; \
	echo "[FW] RAM usage (.data + .bss): $$ram_used / $$ram_total bytes ($$ram_pct%)"; \
	echo "[FW] HEX file size: $$hex_file_bytes bytes"; \
	echo "[FW] HEX payload bytes: $$hex_used_bytes bytes"; \
	echo "[FW] ROM usage: $$hex_used_bytes / $$rom_cap bytes ($$rom_pct%)"; \
	if [ $$hex_used_bytes -gt $$rom_cap ]; then \
		echo "[FW][ERROR] Firmware payload exceeds wb_rom capacity by $$((hex_used_bytes-rom_cap)) bytes"; \
		exit 2; \
	fi

clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf $(CACHE_ROOT)

# ─── Memory Lab (toolchain / startup validation) ──────────────────────────────
#
# Builds memoryLab.cpp as a standalone firmware (no u8g2, no u8g2_hal).
# Produces three analysis files alongside the hex:
#   build/lab.map  — linker memory map: section sizes, symbol addresses
#   build/lab.lst  — full disassembly with source (check gp init, BSS loop, ctors)
#
# Usage:
#   make lab        → build everything
#   make lab-fw     → build + program via ecpbram BRAM patch (from root Makefile)

LAB_ELF     = $(BUILD)/lab.elf
LAB_HEX     = $(BUILD)/lab.hex
LAB_LST     = $(BUILD)/lab.lst
LAB_MAP     = $(BUILD)/lab.map
LAB_OBJ     = $(BUILD)/memoryLab.o

# -g: debug symbols in ELF so objdump -S can interleave C++ source in .lst
LAB_CXXFLAGS = $(CXXFLAGS) -g

LAB_LDFLAGS = -T link.ld \
              -nostartfiles \
              -Wl,--gc-sections \
              -Wl,-Map,$(LAB_MAP) \
              --specs=nano.specs \
              --specs=nosys.specs

.PHONY: lab

lab: $(LAB_HEX) $(LAB_LST)
	@echo "────────────────────────────────────────────────"
	@echo "  Lab firmware: $(LAB_HEX)"
	@echo "  Memory map:   $(LAB_MAP)"
	@echo "  Disassembly:  $(LAB_LST)"
	@echo "────────────────────────────────────────────────"

# ELF: start + memoryLab + i2c only (no u8g2)
$(LAB_ELF): $(BUILD)/start.o $(LAB_OBJ) $(BUILD)/i2c.o | $(BUILD)
	$(CROSS)g++ $(LAB_LDFLAGS) $^ -o $@

# Compile memoryLab.cpp with debug symbols
$(LAB_OBJ): memoryLab.cpp | $(BUILD)
	$(CROSS)g++ $(LAB_CXXFLAGS) -c $< -o $@

# Intel/Verilog hex for BRAM programming
$(LAB_HEX): $(LAB_ELF)
	$(CROSS)objcopy -O verilog $< $@

# Full disassembly: -d=disasm, -S=interleave source, -l=file:line, -M no-aliases
$(LAB_LST): $(LAB_ELF)
	$(CROSS)objdump -d -S -l -M no-aliases $< > $@

-include $(DEPS)