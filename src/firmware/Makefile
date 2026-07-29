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
DEPS=$(BUILD)/start.d $(MAIN_OBJ:.o=.d) $(BUILD)/u8g2_hal.d $(U8G2_OBJS:.o=.d)

CFLAGS=-march=rv32im_zicsr -mabi=ilp32 \
       -Wall \
       -Os \
       -ffreestanding \
       -ffunction-sections \
       -fdata-sections \
        -MMD \
        -MP \
       -I$(U8G2_DIR) \
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
LDFLAGS=-T link.ld \
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

$(BUILD)/$(TARGET).elf: $(BUILD)/start.o $(MAIN_OBJ) $(BUILD)/u8g2_hal.o $(U8G2_LIB) | $(BUILD)
	$(CROSS)g++ $(LDFLAGS) $^ -o $@

$(BUILD)/start.o: start.S | $(BUILD)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(MAIN_OBJ): $(MAIN_SRC) | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

$(BUILD)/u8g2_hal.o: lib/u8g2_hal/u8g2_hal.cpp | $(BUILD)
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

-include $(DEPS)