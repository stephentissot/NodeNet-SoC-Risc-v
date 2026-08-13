DEVICE=45k
PACKAGE=CABGA381
SPEED=6

TOP=top

BUILD=build

LPF=constraints/colorlight_i9.lpf
OPENOCD_SCRIPTS ?= D:/oss-cad-suite/share/openocd/scripts
# CMSIS-DAP v1 probes are often unstable above 100 kHz on ECP5 JTAG chains.
ECPDAP_FREQ ?= 100k
PYTHON ?= python

FIRMWARE_HEX=src/firmware/build/boot_stage0.hex
FIRMWARE_IMAGE=src/firmware/build/nodenet_riscv_app.img
FW_IMAGE_FLASH_OFFSET=0x244000
FW_IMAGE_MIN_OFFSET=0x244000
FLASH_TOTAL_BYTES=0x800000
FW_IMAGE_SLOT_BYTES=0x5BC000
FW_IMAGE_VERIFY_TOOL=src/firmware/tools/verify_firmware_image.py
FIRMWARE_PREV_HEX=$(BUILD)/nodenet_riscv.prev.hex
FIRMWARE_PADDED_HEX=$(BUILD)/nodenet_riscv.padded.hex
FIRMWARE_PREV_PADDED_HEX=$(BUILD)/nodenet_riscv.prev_padded.hex
FW_PATCH_CONFIG=$(BUILD)/$(TOP)_fw.config
FW_PATCH_BIT=$(BUILD)/$(TOP)_fw.bit
FLASH_BOOT_IMAGE=$(BUILD)/$(TOP)_flash.bit
ROM_BYTES=65536

# Synthesis sources.
# Keep this explicit so the build uses the Alex Forencich RTL from src/verilog-i2c/rtl
# and does not accidentally pull in testbenches or any legacy i2c directory.
SOURCES := src/top.sv \
           $(wildcard src/wbDevices/*.sv) \
           src/picorv32/picorv32.v \
           $(wildcard src/uart/*.v) \
           $(wildcard src/verilog-i2c/rtl/*.v)

SOURCES := $(sort $(SOURCES))


.PHONY: all firmware-build firmware-test firmware-image firmware-bootloader flash-fw-check flash-fw flash-fw-run bringup clean clean-firmware lock-flash unlock-flash ram-fast ram-fw fw firmware-only

all: firmware-build $(BUILD)/$(TOP).bit

fw firmware-only: firmware-build

LAB_FIRMWARE_HEX     = src/firmware/build/lab.hex
LAB_PADDED_HEX       = $(BUILD)/lab.padded.hex

.PHONY: lab lab-fw

# Build the lab firmware (produces build/lab.{hex,lst,map} in src/firmware/build/)
lab:
	$(MAKE) -C src/firmware lab

# Program lab firmware into FPGA BRAM via ecpbram patch.
# Requires: 'make all' done at least once (provides top.config as BRAM baseline).
# Uses the current nodenet_riscv.hex as the "from" image for ecpbram.
lab-fw: lab
	@if [ ! -f $(BUILD)/$(TOP).config ]; then \
		echo "Missing $(BUILD)/$(TOP).config — run 'make all' first."; \
		exit 1; \
	fi
	@if [ ! -f $(FIRMWARE_HEX) ]; then \
		echo "Missing $(FIRMWARE_HEX) — run 'make firmware-build' first."; \
		exit 1; \
	fi
	awk -v depth=$(ROM_BYTES) \
	    'BEGIN{addr=0} {for(i=1;i<=NF;i++){tok=toupper($$i); if(tok~/^@/){addr=strtonum("0x" substr(tok,2));} else {mem[addr]=tok; addr++}}} END{for(i=0;i<depth;i++) print (i in mem)?mem[i]:"00"}' \
	    $(FIRMWARE_HEX) > $(FIRMWARE_PREV_PADDED_HEX)
	awk -v depth=$(ROM_BYTES) \
	    'BEGIN{addr=0} {for(i=1;i<=NF;i++){tok=toupper($$i); if(tok~/^@/){addr=strtonum("0x" substr(tok,2));} else {mem[addr]=tok; addr++}}} END{for(i=0;i<depth;i++) print (i in mem)?mem[i]:"00"}' \
	    $(LAB_FIRMWARE_HEX) > $(LAB_PADDED_HEX)
	@if ecpbram \
	        --input  $(BUILD)/$(TOP).config \
	        --output $(FW_PATCH_CONFIG) \
	        --from   $(FIRMWARE_PREV_PADDED_HEX) \
	        --to     $(LAB_PADDED_HEX) && \
	   ecppack --compress $(FW_PATCH_CONFIG) $(FW_PATCH_BIT); then \
	    openFPGALoader -b colorlight-i9 $(FW_PATCH_BIT); \
	else \
	    echo "ecpbram patch failed — run 'make all' to rebuild the full bitstream."; \
	    exit 1; \
	fi

# Default firmware build uses src/firmware/main.cpp.
firmware-build:
	$(MAKE) -C src/firmware bootloader-build ROM_CAPACITY_BYTES=$(ROM_BYTES)

firmware-image:
	$(MAKE) -C src/firmware firmware-image ROM_CAPACITY_BYTES=$(ROM_BYTES)

firmware-bootloader:
	$(MAKE) -C src/firmware bootloader-build ROM_CAPACITY_BYTES=$(ROM_BYTES)

# Program only the stage0 application image into SPI flash at fixed partition offset.
# This avoids FPGA synthesis/P&R when firmware changes and stage0 stays unchanged.
flash-fw-check: firmware-image
	@img_size=$$(wc -c < $(FIRMWARE_IMAGE)); \
	offset=$$(( $(FW_IMAGE_FLASH_OFFSET) )); \
	min_off=$$(( $(FW_IMAGE_MIN_OFFSET) )); \
	flash_total=$$(( $(FLASH_TOTAL_BYTES) )); \
	slot_size=$$(( $(FW_IMAGE_SLOT_BYTES) )); \
	if [ $$offset -lt $$min_off ]; then \
		echo "[FWIMG][ERROR] FW_IMAGE_FLASH_OFFSET is below allowed minimum"; \
		exit 2; \
	fi; \
	if [ $$img_size -gt $$slot_size ]; then \
		echo "[FWIMG][ERROR] image too large for slot: $$img_size > $$slot_size"; \
		exit 2; \
	fi; \
	end_off=$$((offset + img_size)); \
	if [ $$end_off -gt $$flash_total ]; then \
		echo "[FWIMG][ERROR] image exceeds flash range: end=$$end_off total=$$flash_total"; \
		exit 2; \
	fi; \
	$(PYTHON) $(FW_IMAGE_VERIFY_TOOL) --input $(FIRMWARE_IMAGE)

flash-fw: flash-fw-check
	openocd \
		-s $(OPENOCD_SCRIPTS) \
		-f interface/cmsis-dap.cfg \
		-c "transport select jtag" \
		-f fpga/lattice_ecp5.cfg \
		-c "set JTAGSPI_CHAIN_ID ecp5.pld; \
		    source [find cpld/jtagspi.cfg]; \
		    init; \
		    jtagspi_init ecp5.pld \"\" -1; \
		    flash protect 0 0 last off; \
		    flash write_image erase unlock $(FIRMWARE_IMAGE) $(FW_IMAGE_FLASH_OFFSET); \
		    flash verify_image $(FIRMWARE_IMAGE) $(FW_IMAGE_FLASH_OFFSET); \
		    exit"

# End-to-end firmware-only cycle: build+verify image, program flash partition, then
# reload the current bitstream in SRAM to restart stage0 without synthesis/P&R.
flash-fw-run: flash-fw
	$(MAKE) ram-fast


# Ensure firmware is rebuilt before any target that consumes the hex.
$(FIRMWARE_HEX): firmware-build
	@test -f $@ || (echo "Missing $@ after firmware-build" && exit 1)

# Build test firmware (src/firmware/test_main.cpp) without manual MAIN_SRC override.
firmware-test:
	$(MAKE) -C src/firmware MAIN_SRC=test_main.cpp ROM_CAPACITY_BYTES=$(ROM_BYTES)

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

# 	yosys \
# 		-p "read_verilog -sv $(SOURCES); \
# 		    synth_ecp5 -top $(TOP); \
# 		    write_json $@; \
# 		    write_verilog $(BUILD)/$(TOP)_post_synth.v"

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


# Generate SPI Flash image for cold boot (bootaddr=0)
flash_image: $(BUILD)/$(TOP).config
	ecppack \
		--compress \
		--bootaddr 0 \
		$< \
		$(FLASH_BOOT_IMAGE)
	@echo "Using $(FLASH_BOOT_IMAGE) as SPI flash image"


# Program W25Q64 SPI Flash
#
# Primary flow uses ecpdap as requested:
#   ecpdap program <bit>
#   ecpdap flash erase
#   ecpdap flash write <bit>
#
# Falls back to OpenOCD jtagspi, then openFPGALoader.
flash: flash_image
	@if ecpdap -f $(ECPDAP_FREQ) program $(BUILD)/$(TOP).bit && \
		ecpdap -f $(ECPDAP_FREQ) flash unprotect && \
		ecpdap -f $(ECPDAP_FREQ) flash erase && \
		ecpdap -f $(ECPDAP_FREQ) flash write $(FLASH_BOOT_IMAGE) && \
		ecpdap -f $(ECPDAP_FREQ) flash jump write 0x0 --spimode read && \
		ecpdap -f $(ECPDAP_FREQ) flash jump read; then \
		echo "ecpdap flash succeeded."; \
	elif openocd \
		-s $(OPENOCD_SCRIPTS) \
		-f interface/cmsis-dap.cfg \
		-c "transport select jtag" \
		-f fpga/lattice_ecp5.cfg \
		-c "set JTAGSPI_CHAIN_ID ecp5.pld; \
		    source [find cpld/jtagspi.cfg]; \
		    init; \
		    jtagspi_init ecp5.pld \"\" -1; \
		    flash write_image erase unlock $(FLASH_BOOT_IMAGE) 0x0; \
		    exit"; then \
		echo "OpenOCD flash succeeded (fallback)."; \
	else \
		echo "ecpdap/OpenOCD failed, retrying with openFPGALoader..."; \
		openFPGALoader -b colorlight-i9 -f --verify $(FLASH_BOOT_IMAGE); \
	fi


# Disable flash protection before manual flash operations.
# Note: availability depends on the OpenOCD flash driver stack.
# ecpdap equivalent:
#   ecpdap flash unprotect
unlock-flash:
	openocd \
		-s $(OPENOCD_SCRIPTS) \
		-f interface/cmsis-dap.cfg \
		-c "transport select jtag" \
		-f fpga/lattice_ecp5.cfg \
		-c "set JTAGSPI_CHAIN_ID ecp5.pld; \
		    source [find cpld/jtagspi.cfg]; \
		    init; \
		    jtagspi_init ecp5.pld \"\" -1; \
		    flash protect 0 0 last off; \
		    flash info 0; \
		    exit"


# Re-enable flash protection after programming if desired.
# ecpdap equivalent:
#   ecpdap flash protect
lock-flash:
	openocd \
		-s $(OPENOCD_SCRIPTS) \
		-f interface/cmsis-dap.cfg \
		-c "transport select jtag" \
		-f fpga/lattice_ecp5.cfg \
		-c "set JTAGSPI_CHAIN_ID ecp5.pld; \
		    source [find cpld/jtagspi.cfg]; \
		    init; \
		    jtagspi_init ecp5.pld \"\" -1; \
		    flash protect 0 0 last on; \
		    flash info 0; \
		    exit"
sources:
	@echo $(SOURCES)