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
SDRAM_TEST_SRC=sdram.cpp
SDRAM_TEST_OBJ=$(BUILD)/sdram.o

DEPS=$(BUILD)/start.d $(MAIN_OBJ:.o=.d) $(BUILD)/u8g2_hal.d $(BUILD)/memoryLab.d $(BUILD)/i2c.d $(BUILD)/Serial.d $(BUILD)/sdram.d $(U8G2_OBJS:.o=.d)

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
        -I$(SERIAL_DIR) \
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

all: $(BUILD)/$(TARGET).hex

$(BUILD):
	mkdir -p $(BUILD)

$(U8G2_CACHE):
	mkdir -p $@

$(BUILD)/$(TARGET).elf: $(BUILD)/start.o $(MAIN_OBJ) $(BUILD)/u8g2_hal.o $(I2C_OBJ) $(SERIAL_OBJ) $(SDRAM_TEST_OBJ) $(U8G2_LIB) | $(BUILD)
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