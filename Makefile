DEVICE=45k
PACKAGE=CABGA381
SPEED=6

TOP=top

BUILD=build

LPF=constraints/colorlight_i9.lpf

FIRMWARE_HEX=src/firmware/build/nodenet_riscv.hex
FIRMWARE_PREV_HEX=$(BUILD)/nodenet_riscv.prev.hex
FIRMWARE_PADDED_HEX=$(BUILD)/nodenet_riscv.padded.hex
FIRMWARE_PREV_PADDED_HEX=$(BUILD)/nodenet_riscv.prev_padded.hex
FW_PATCH_CONFIG=$(BUILD)/$(TOP)_fw.config
FW_PATCH_BIT=$(BUILD)/$(TOP)_fw.bit
ROM_BYTES=16384

# Recursive source discovery
rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))

SOURCES := $(call rwildcard,src/,*.sv) \
           $(call rwildcard,src/,*.v)


.PHONY: all firmware-build firmware-test bringup clean clean-firmware lock-flash unlock-flash ram-fast ram-fw

all: firmware-build $(BUILD)/$(TOP).bit

# Default firmware build uses src/firmware/main.cpp.
firmware-build:
	$(MAKE) -C src/firmware


# Ensure firmware is rebuilt before any target that consumes the hex.
$(FIRMWARE_HEX): firmware-build
	@test -f $@ || (echo "Missing $@ after firmware-build" && exit 1)

# Build test firmware (src/firmware/test_main.cpp) without manual MAIN_SRC override.
firmware-test:
	$(MAKE) -C src/firmware MAIN_SRC=test_main.cpp

# Build complete bring-up image (test firmware + FPGA bitstream).
bringup: firmware-test $(BUILD)/$(TOP).bit

clean-firmware:
	$(MAKE) -C src/firmware clean

clean: clean-firmware
	rm -rf $(BUILD)


$(BUILD):
	mkdir -p $(BUILD)


# Synthesis
$(BUILD)/$(TOP).json: $(SOURCES) $(FIRMWARE_HEX) | $(BUILD)
	yosys \
		-p "read_verilog -sv $(SOURCES); \
		    synth_ecp5 -top $(TOP) -json $@"


# Place & Route
$(BUILD)/$(TOP).config: $(BUILD)/$(TOP).json
	nextpnr-ecp5 \
		--$(DEVICE) \
		--package $(PACKAGE) \
		--speed $(SPEED) \
		--json $< \
		--lpf $(LPF) \
		--textcfg $@


# Bitstream generation
$(BUILD)/$(TOP).bit: $(BUILD)/$(TOP).config
	ecppack \
		--compress \
		$< \
		$@


# Generate SVF for RAM programming
$(BUILD)/$(TOP).svf: $(BUILD)/$(TOP).bit
	ecppack \
		--svf $@ \
		$<


# Program FPGA configuration RAM (default: openFPGALoader)
ram: $(BUILD)/$(TOP).bit
	openFPGALoader -b colorlight-i9 $<


# Program FPGA RAM with the last built bitstream without triggering rebuilds.
ram-fast:
	@if [ ! -f $(BUILD)/$(TOP).bit ]; then \
		echo "Missing $(BUILD)/$(TOP).bit. Run 'make all' once first."; \
		exit 1; \
	fi
	openFPGALoader -b colorlight-i9 $(BUILD)/$(TOP).bit


# Firmware-only update path:
# 1) save previous firmware hex
# 2) rebuild firmware
# 3) patch existing .config BRAM init using ecpbram
# 4) repack + program patched bitstream
#
# Requires a previously built $(BUILD)/$(TOP).config from 'make all'.
ram-fw:
	@if [ ! -f $(BUILD)/$(TOP).config ]; then \
		echo "Missing $(BUILD)/$(TOP).config. Run 'make all' once first."; \
		exit 1; \
	fi
	@if [ ! -f $(FIRMWARE_HEX) ]; then \
		echo "Missing $(FIRMWARE_HEX). Building firmware baseline first."; \
		$(MAKE) firmware-build; \
	fi
	cp $(FIRMWARE_HEX) $(FIRMWARE_PREV_HEX)
	$(MAKE) firmware-build
	awk -v depth=$(ROM_BYTES) 'BEGIN{addr=0} {for(i=1;i<=NF;i++){tok=toupper($$i); if(tok ~ /^@/){addr=strtonum("0x" substr(tok,2));} else {mem[addr]=tok; addr++}}} END{for(i=0;i<depth;i++) print (i in mem)?mem[i]:"00"}' $(FIRMWARE_PREV_HEX) > $(FIRMWARE_PREV_PADDED_HEX)
	awk -v depth=$(ROM_BYTES) 'BEGIN{addr=0} {for(i=1;i<=NF;i++){tok=toupper($$i); if(tok ~ /^@/){addr=strtonum("0x" substr(tok,2));} else {mem[addr]=tok; addr++}}} END{for(i=0;i<depth;i++) print (i in mem)?mem[i]:"00"}' $(FIRMWARE_HEX) > $(FIRMWARE_PADDED_HEX)
	@if ecpbram --input $(BUILD)/$(TOP).config --output $(FW_PATCH_CONFIG) --from $(FIRMWARE_PREV_PADDED_HEX) --to $(FIRMWARE_PADDED_HEX) && \
		ecppack --compress $(FW_PATCH_CONFIG) $(FW_PATCH_BIT); then \
		openFPGALoader -b colorlight-i9 $(FW_PATCH_BIT); \
	else \
		echo "ecpbram patch failed, fallback to full rebuild/program (make ram)."; \
		$(MAKE) ram; \
	fi


# Generate SPI Flash image
flash_image: $(BUILD)/$(TOP).bit
	ecppack \
		--compress \
		--bootaddr 0 \
		$< \
		$(BUILD)/$(TOP).config


# Program W25Q64 SPI Flash
#
# ecpdap fallback (if OpenOCD/jtagspi flow does not work on a given setup):
#   ecpdap flash unprotect
#   ecpdap flash write $(BUILD)/$(TOP).config
flash: flash_image
	openocd \
		-f interface/cmsis-dap.cfg \
		-f target/lattice-ecp5.cfg \
		-c "init; \
		    jtagspi_init 0 0x20000000; \
		    flash probe 0; \
		    flash write_image erase unlock $(BUILD)/$(TOP).config 0x0; \
		    exit"


# Disable flash protection before manual flash operations.
# Note: availability depends on the OpenOCD flash driver stack.
# ecpdap equivalent:
#   ecpdap flash unprotect
unlock-flash:
	openocd \
		-f interface/cmsis-dap.cfg \
		-f target/lattice-ecp5.cfg \
		-c "init; \
		    jtagspi_init 0 0x20000000; \
		    flash probe 0; \
		    flash protect 0 0 last off; \
		    flash info 0; \
		    exit"


# Re-enable flash protection after programming if desired.
# ecpdap equivalent:
#   ecpdap flash protect
lock-flash:
	openocd \
		-f interface/cmsis-dap.cfg \
		-f target/lattice-ecp5.cfg \
		-c "init; \
		    jtagspi_init 0 0x20000000; \
		    flash probe 0; \
		    flash protect 0 0 last on; \
		    flash info 0; \
		    exit"
sources:
	@echo $(SOURCES)