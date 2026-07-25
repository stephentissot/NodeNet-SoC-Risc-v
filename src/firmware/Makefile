CROSS=riscv-none-elf-

CFLAGS=-march=rv32im -mabi=ilp32 \
       -Wall \
       -Os \
       -ffreestanding \
       -nostdlib

LDFLAGS=-T link.ld \
        -nostdlib

BUILD=build

all: $(BUILD)/blink.hex

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/blink.elf: $(BUILD)/start.o $(BUILD)/main.o | $(BUILD)
	$(CROSS)gcc $(LDFLAGS) $^ -o $@

$(BUILD)/start.o: start.S | $(BUILD)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(BUILD)/main.o: main.c | $(BUILD)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(BUILD)/blink.hex: $(BUILD)/blink.elf
	$(CROSS)objcopy \
		-O verilog \
		$< \
		$@

clean:
	rm -rf $(BUILD)