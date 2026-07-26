CROSS=riscv-none-elf-

# u8g2 library location
U8G2_DIR=lib/u8g2/csrc
U8G2_SRCS=$(wildcard $(U8G2_DIR)/*.c)
U8G2_OBJS=$(patsubst $(U8G2_DIR)/%.c,$(BUILD)/u8g2/%.o,$(U8G2_SRCS))

CFLAGS=-march=rv32im -mabi=ilp32 \
       -Wall \
       -Os \
       -ffreestanding \
       -ffunction-sections \
       -fdata-sections \
       -I$(U8G2_DIR)

# C++ flags: no exceptions, no RTTI (keeps code small on bare-metal)
CXXFLAGS=$(CFLAGS) \
         -std=c++17 \
         -fno-exceptions \
         -fno-rtti \
         -fno-threadsafe-statics \
         -fno-use-cxa-atexit

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

all: $(BUILD)/blink.hex

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/u8g2: | $(BUILD)
	mkdir -p $@

$(BUILD)/blink.elf: $(BUILD)/start.o $(BUILD)/main.o $(BUILD)/u8g2_hal.o $(U8G2_OBJS) | $(BUILD)
	$(CROSS)g++ $(LDFLAGS) $^ -o $@

$(BUILD)/start.o: start.S | $(BUILD)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(BUILD)/main.o: main.cpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

$(BUILD)/u8g2_hal.o: u8g2_hal.cpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@

# u8g2 C sources (compiled with gcc, not g++)
$(BUILD)/u8g2/%.o: $(U8G2_DIR)/%.c | $(BUILD)/u8g2
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(BUILD)/blink.hex: $(BUILD)/blink.elf
	$(CROSS)objcopy \
		-O verilog \
		$< \
		$@

clean:
	rm -rf $(BUILD)