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
ifneq ($(wildcard .venv/Scripts/python.exe),)
PYTHON := .venv/Scripts/python.exe
else ifneq ($(wildcard .venv/bin/python),)
PYTHON := .venv/bin/python
endif
FW_STRICT_VERIFY ?= 0

FIRMWARE_HEX=src/firmware/build/boot_stage0.hex
FIRMWARE_IMAGE=src/firmware/build/nodenet_riscv_app.img
FIRMWARE_IMAGE_BAD_CRC=src/firmware/build/nodenet_riscv_app.bad_crc.img
FIRMWARE_IMAGE_BAD_SIZE=src/firmware/build/nodenet_riscv_app.bad_size.img
FIRMWARE_IMAGE_MISSING=src/firmware/build/nodenet_riscv_app.missing.img
FW_IMAGE_FLASH_OFFSET=0x244000
FW_IMAGE_MIN_OFFSET=0x244000
FLASH_TOTAL_BYTES=0x800000
FW_IMAGE_SLOT_BYTES=0x5BC000
FW_IMAGE_VERIFY_TOOL=src/firmware/tools/verify_firmware_image.py
FW_IMAGE_TEST_TOOL=src/firmware/tools/make_boot_test_images.py
PLC_PACKAGE_PACK_TOOL=src/firmware/tools/pack_plc_linked_package.py
PLC_PACKAGE_VERIFY_TOOL=src/firmware/tools/verify_plc_linked_package.py
PLC_MIRROR_PACKAGE_TOOL=src/firmware/tools/pack_plc_mirror_program.py
PLC_LINKED_CODE_INPUT ?=
PLC_PACKAGE_IMAGE ?= src/firmware/build/plc_linked_package.img
PLC_MIRROR_PAIRS ?=
PLC_PACKAGE_FLASH_OFFSET ?= 0x204000
PLC_PACKAGE_SLOT_BYTES ?= 0x20000
PLC_PACKAGE_ABI_VERSION ?= 1
PLC_PACKAGE_FLAGS ?= 0
PLC_PACKAGE_ENTRY_OFFSET ?= 0
PLC_PACKAGE_SYMBOL_COUNT ?= 0
PLC_PACKAGE_RELOCATION_COUNT ?= 0
PLC_PACKAGE_MAX_INSTRUCTIONS ?= 200
PLC_PACKAGE_MAX_SCAN_US ?= 10000
PLC_PACKAGE_RUNTIME_HEADER_ADDR ?= 0x20100000
PLC_PACKAGE_STORE_EPOCH ?= 1
FW_SKIP_RESET ?= 0
FIRMWARE_PREV_HEX=$(BUILD)/nodenet_riscv.prev.hex
FIRMWARE_PADDED_HEX=$(BUILD)/nodenet_riscv.padded.hex
FIRMWARE_PREV_PADDED_HEX=$(BUILD)/nodenet_riscv.prev_padded.hex
FW_PATCH_CONFIG=$(BUILD)/$(TOP)_fw.config
FW_PATCH_BIT=$(BUILD)/$(TOP)_fw.bit
FLASH_BOOT_IMAGE=$(BUILD)/$(TOP)_flash.bit
FLASH_PROGRAM_IMAGE=$(BUILD)/$(TOP).bit
ROM_BYTES=65536
LITEDRAM_BUILD_DIR ?= $(BUILD)/litedram
LITEDRAM_CONFIG ?= tools/litedram/colorlight_i9.yml
LITEDRAM_NAME ?= litedram_core
LITEDRAM_GENERATED_RTL ?= $(LITEDRAM_BUILD_DIR)/gateware/$(LITEDRAM_NAME).v
LITEDRAM_RTL ?= src/sdram/$(LITEDRAM_NAME).v
LITEDRAM_VALID_STAMP ?= $(BUILD)/.litedram_rtl_valid.stamp
YOSYS_DEFINES :=
LITEDRAM_DEPS := $(LITEDRAM_RTL) $(LITEDRAM_VALID_STAMP)
FIRMWARE_DEPS := src/firmware/Makefile \
				 src/bootloader/boot_stage0.cpp \
				 src/bootloader/start.S \
				 src/bootloader/link.ld \
				 src/firmware/lib/flash/flash.cpp \
				 src/firmware/lib/flash/flash.h \
				 $(wildcard src/firmware/include/*.h)

# Synthesis sources.
# Keep this explicit so the build uses the Alex Forencich RTL from src/verilog-i2c/rtl
# and does not accidentally pull in testbenches or any legacy i2c directory.
SOURCES := src/top.sv \
           $(wildcard src/wbDevices/*.sv) \
	   $(wildcard src/modbus/*.sv) \
		   $(wildcard src/sdram/*.sv) \
           src/picorv32/picorv32.v \
           $(wildcard src/uart/*.v) \
           $(wildcard src/verilog-i2c/rtl/*.v)

SOURCES += $(LITEDRAM_RTL)

SOURCES := $(sort $(SOURCES))


.PHONY: all firmware-build firmware-test firmware-image firmware-bootloader flash-fw-check flash-fw-check-image flash-fw flash-fw-write-image flash-fw-run firmware-image-tests flash-fw-test-missing flash-fw-test-size flash-fw-test-crc plc-package plc-mirror-package plc-package-check plc-package-build-check plc-mirror-package-check flash-plc-package flash-plc-package-write bringup clean clean-firmware lock-flash unlock-flash ram-fast ram-fw fw firmware-only litedram-gen litedram-copy litedram-refresh

all: firmware-build $(BUILD)/$(TOP).bit

litedram-gen:
	$(PYTHON) tools/generate_litedram_core.py --config $(LITEDRAM_CONFIG) --output-dir $(LITEDRAM_BUILD_DIR) --name $(LITEDRAM_NAME)


$(LITEDRAM_GENERATED_RTL): tools/generate_litedram_core.py $(LITEDRAM_CONFIG)
	@mkdir -p $(LITEDRAM_BUILD_DIR)
	$(PYTHON) tools/generate_litedram_core.py --config $(LITEDRAM_CONFIG) --output-dir $(LITEDRAM_BUILD_DIR) --name $(LITEDRAM_NAME)
	@test -s $(LITEDRAM_GENERATED_RTL) || (echo "[LITEDRAM][ERROR] Generated RTL is empty: $(LITEDRAM_GENERATED_RTL)"; rm -f $(LITEDRAM_GENERATED_RTL); exit 1)
	@grep -Eq "^[[:space:]]*module[[:space:]]+$(LITEDRAM_NAME)[[:space:]#(]" $(LITEDRAM_GENERATED_RTL) || (echo "[LITEDRAM][ERROR] Top module $(LITEDRAM_NAME) not found in $(LITEDRAM_GENERATED_RTL)"; rm -f $(LITEDRAM_GENERATED_RTL); exit 1)


litedram-copy: $(LITEDRAM_GENERATED_RTL)
	@mkdir -p src/sdram
	cp $(LITEDRAM_GENERATED_RTL) $(LITEDRAM_RTL)
	@echo "[LITEDRAM] Copied $(LITEDRAM_GENERATED_RTL) -> $(LITEDRAM_RTL)"

litedram-refresh: litedram-gen litedram-copy

$(LITEDRAM_VALID_STAMP): $(LITEDRAM_RTL)
	@test -s $(LITEDRAM_RTL) || (echo "[LITEDRAM][ERROR] RTL is empty: $(LITEDRAM_RTL)"; rm -f $(LITEDRAM_RTL); exit 1)
	@grep -Eq "^[[:space:]]*module[[:space:]]+$(LITEDRAM_NAME)[[:space:]#(]" $(LITEDRAM_RTL) || (echo "[LITEDRAM][ERROR] Top module $(LITEDRAM_NAME) not found in $(LITEDRAM_RTL)"; rm -f $(LITEDRAM_RTL); exit 1)
	@touch $(LITEDRAM_VALID_STAMP)

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

firmware-image-tests: firmware-image
	$(PYTHON) $(FW_IMAGE_TEST_TOOL) \
		--input $(FIRMWARE_IMAGE) \
		--out-dir src/firmware/build \
		--prefix nodenet_riscv_app

firmware-bootloader:
	$(MAKE) -C src/firmware bootloader-build ROM_CAPACITY_BYTES=$(ROM_BYTES)

# Program only the stage0 application image into SPI flash at fixed partition offset.
# This avoids FPGA synthesis/P&R when firmware changes and stage0 stays unchanged.
flash-fw-check: firmware-image
	$(MAKE) flash-fw-check-image

flash-fw-check-image:
	@if [ ! -f $(FIRMWARE_IMAGE) ]; then \
		echo "[FWIMG][ERROR] Missing image: $(FIRMWARE_IMAGE)"; \
		exit 2; \
	fi
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
	$(MAKE) IMAGE_TO_FLASH=$(FIRMWARE_IMAGE) flash-fw-write-image

flash-fw-write-image:
	@if [ -z "$(IMAGE_TO_FLASH)" ]; then \
		echo "[FWIMG][ERROR] IMAGE_TO_FLASH is empty"; \
		exit 2; \
	fi
	@if [ ! -f $(IMAGE_TO_FLASH) ]; then \
		echo "[FWIMG][ERROR] Missing image: $(IMAGE_TO_FLASH)"; \
		exit 2; \
	fi
	@img_size=$$(wc -c < $(IMAGE_TO_FLASH)); \
	if [ $$img_size -gt $$(( $(FW_IMAGE_SLOT_BYTES) )) ]; then \
		echo "[FWIMG][ERROR] IMAGE_TO_FLASH larger than slot"; \
		exit 2; \
	fi
	@ofl_verify=""; \
	ofl_reset=""; \
	if [ "$(FW_STRICT_VERIFY)" = "1" ]; then \
		ofl_verify="--verify"; \
	fi; \
	if [ "$(FW_SKIP_RESET)" = "1" ]; then \
		ofl_reset="--skip-reset"; \
	fi; \
	echo "[FWIMG] openFPGALoader write offset=$(FW_IMAGE_FLASH_OFFSET) strict=$(FW_STRICT_VERIFY) skip_reset=$(FW_SKIP_RESET)"; \
	openFPGALoader -b colorlight-i9 -f $$ofl_reset --unprotect-flash $$ofl_verify -o $(FW_IMAGE_FLASH_OFFSET) $(IMAGE_TO_FLASH)

# End-to-end firmware-only cycle: build+verify image, program flash partition, then
# reload the current bitstream in SRAM to restart stage0 without synthesis/P&R.
flash-fw-run: flash-fw
	$(MAKE) ram-fast

flash-fw-test-missing: firmware-image-tests
	$(MAKE) IMAGE_TO_FLASH=$(FIRMWARE_IMAGE_MISSING) flash-fw-write-image

flash-fw-test-size: firmware-image-tests
	$(MAKE) IMAGE_TO_FLASH=$(FIRMWARE_IMAGE_BAD_SIZE) flash-fw-write-image

flash-fw-test-crc: firmware-image-tests
	$(MAKE) IMAGE_TO_FLASH=$(FIRMWARE_IMAGE_BAD_CRC) flash-fw-write-image

plc-package:
	@if [ -z "$(PLC_LINKED_CODE_INPUT)" ]; then \
		echo "[PLCPKG][ERROR] PLC_LINKED_CODE_INPUT is empty"; \
		exit 2; \
	fi
	@if [ ! -f $(PLC_LINKED_CODE_INPUT) ]; then \
		echo "[PLCPKG][ERROR] Missing linked code input: $(PLC_LINKED_CODE_INPUT)"; \
		exit 2; \
	fi
	$(PYTHON) $(PLC_PACKAGE_PACK_TOOL) \
		--input $(PLC_LINKED_CODE_INPUT) \
		--output $(PLC_PACKAGE_IMAGE) \
		--abi-version $(PLC_PACKAGE_ABI_VERSION) \
		--flags $(PLC_PACKAGE_FLAGS) \
		--entry-offset $(PLC_PACKAGE_ENTRY_OFFSET) \
		--symbol-count $(PLC_PACKAGE_SYMBOL_COUNT) \
		--relocation-count $(PLC_PACKAGE_RELOCATION_COUNT) \
		--max-instructions-per-scan $(PLC_PACKAGE_MAX_INSTRUCTIONS) \
		--max-scan-time-us $(PLC_PACKAGE_MAX_SCAN_US) \
		--runtime-header-addr $(PLC_PACKAGE_RUNTIME_HEADER_ADDR) \
		--store-epoch $(PLC_PACKAGE_STORE_EPOCH)

plc-mirror-package:
	@if [ -z "$(PLC_MIRROR_PAIRS)" ]; then \
		echo "[PLCMIRROR][ERROR] PLC_MIRROR_PAIRS is empty (expected INPUT:OUTPUT[,INPUT:OUTPUT...])"; \
		exit 2; \
	fi
	@pair_args=$$(printf '%s' "$(PLC_MIRROR_PAIRS)" | awk -F',' '{for (i = 1; i <= NF; ++i) printf " --pair %s", $$i}'); \
	$(PYTHON) $(PLC_MIRROR_PACKAGE_TOOL) $$pair_args \
		--output $(PLC_PACKAGE_IMAGE) \
		--abi-version $(PLC_PACKAGE_ABI_VERSION) \
		--flags $(PLC_PACKAGE_FLAGS) \
		--entry-offset $(PLC_PACKAGE_ENTRY_OFFSET) \
		--symbol-count 0 \
		--relocation-count $(PLC_PACKAGE_RELOCATION_COUNT) \
		--max-instructions-per-scan $(PLC_PACKAGE_MAX_INSTRUCTIONS) \
		--max-scan-time-us $(PLC_PACKAGE_MAX_SCAN_US) \
		--runtime-header-addr $(PLC_PACKAGE_RUNTIME_HEADER_ADDR) \
		--store-epoch $(PLC_PACKAGE_STORE_EPOCH)

plc-package-check:
	@if [ ! -f $(PLC_PACKAGE_IMAGE) ]; then \
		echo "[PLCPKG][ERROR] Missing package: $(PLC_PACKAGE_IMAGE)"; \
		exit 2; \
	fi
	@pkg_size=$$(wc -c < $(PLC_PACKAGE_IMAGE)); \
	flash_total=$$(( $(FLASH_TOTAL_BYTES) )); \
	offset=$$(( $(PLC_PACKAGE_FLASH_OFFSET) )); \
	slot_size=$$(( $(PLC_PACKAGE_SLOT_BYTES) )); \
	if [ $$pkg_size -gt $$slot_size ]; then \
		echo "[PLCPKG][ERROR] package too large for slot: $$pkg_size > $$slot_size"; \
		exit 2; \
	fi; \
	end_off=$$((offset + pkg_size)); \
	if [ $$end_off -gt $$flash_total ]; then \
		echo "[PLCPKG][ERROR] package exceeds flash range: end=$$end_off total=$$flash_total"; \
		exit 2; \
	fi
	$(PYTHON) $(PLC_PACKAGE_VERIFY_TOOL) \
		--input $(PLC_PACKAGE_IMAGE) \
		--slot-size $(PLC_PACKAGE_SLOT_BYTES) \
		--expect-runtime-header-addr $(PLC_PACKAGE_RUNTIME_HEADER_ADDR) \
		--expect-store-epoch $(PLC_PACKAGE_STORE_EPOCH)

plc-package-build-check: plc-package plc-package-check

plc-mirror-package-check: plc-mirror-package plc-package-check

flash-plc-package: plc-package-check
	$(MAKE) IMAGE_TO_FLASH=$(PLC_PACKAGE_IMAGE) FLASH_IMAGE_OFFSET=$(PLC_PACKAGE_FLASH_OFFSET) FLASH_IMAGE_SLOT_BYTES=$(PLC_PACKAGE_SLOT_BYTES) flash-plc-package-write

flash-plc-package-write:
	@if [ -z "$(IMAGE_TO_FLASH)" ]; then \
		echo "[PLCPKG][ERROR] IMAGE_TO_FLASH is empty"; \
		exit 2; \
	fi
	@if [ ! -f $(IMAGE_TO_FLASH) ]; then \
		echo "[PLCPKG][ERROR] Missing image: $(IMAGE_TO_FLASH)"; \
		exit 2; \
	fi
	@img_size=$$(wc -c < $(IMAGE_TO_FLASH)); \
	if [ $$img_size -gt $$(( $(FLASH_IMAGE_SLOT_BYTES) )) ]; then \
		echo "[PLCPKG][ERROR] IMAGE_TO_FLASH larger than slot"; \
		exit 2; \
	fi
	@ofl_verify=""; \
	ofl_reset=""; \
	if [ "$(FW_STRICT_VERIFY)" = "1" ]; then \
		ofl_verify="--verify"; \
	fi; \
	if [ "$(FW_SKIP_RESET)" = "1" ]; then \
		ofl_reset="--skip-reset"; \
	fi; \
	echo "[PLCPKG] openFPGALoader write offset=$(FLASH_IMAGE_OFFSET) strict=$(FW_STRICT_VERIFY) skip_reset=$(FW_SKIP_RESET)"; \
	openFPGALoader -b colorlight-i9 -f $$ofl_reset --unprotect-flash $$ofl_verify -o $(FLASH_IMAGE_OFFSET) $(IMAGE_TO_FLASH)


# Ensure bootloader hex tracks firmware source changes, but avoid forcing
# synthesis when nothing changed.
$(FIRMWARE_HEX): $(FIRMWARE_DEPS)
	$(MAKE) -C src/firmware bootloader-build ROM_CAPACITY_BYTES=$(ROM_BYTES)
	@test -f $@ || (echo "Missing $@ after firmware-build" && exit 1)

# Legacy test firmware target kept as an explicit guidance error.
firmware-test:
	@echo "[TEST][ERROR] Legacy test firmware target is not maintained with current APIs."; \
	echo "[TEST][ERROR] Use boot robustness flow in TEST.md (firmware-image-tests + flash-fw-test-*)."; \
	exit 2

# Legacy bringup alias now intentionally triggers firmware-test guidance.
bringup: firmware-test $(BUILD)/$(TOP).bit

clean-firmware:
	$(MAKE) -C src/firmware clean

clean: clean-firmware
	rm -rf $(BUILD)


$(BUILD):
	mkdir -p $(BUILD)


# Synthesis
$(BUILD)/$(TOP).json: $(SOURCES) $(FIRMWARE_HEX) $(LITEDRAM_DEPS) | $(BUILD)
	yosys \
		-p "read_verilog -sv $(YOSYS_DEFINES) $(SOURCES); \
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


# Program FPGA configuration RAM without rebuilding.
# Use 'make all' first if the bitstream is missing.
ram: ram-fast


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
		echo "ecpbram patch failed, fallback to full bitstream rebuild/program (make all + make ram-fast)."; \
		$(MAKE) all; \
		$(MAKE) ram-fast; \
	fi


# Generate an alternate SPI flash image for experiments.
# The official Colorlight i9 flows generally program the raw `.bit` file
# directly into flash rather than a separately repacked boot image.
flash_image: $(BUILD)/$(TOP).config
	ecppack \
		--compress \
		--bootaddr 0 \
		$< \
		$(FLASH_BOOT_IMAGE)
	@echo "Using $(FLASH_BOOT_IMAGE) as SPI flash image"


# Program W25Q64 SPI Flash
#
# Primary flow uses the same high-level Colorlight i9 path found in the
# official repo: unprotect once, then write the raw compressed `.bit` to flash.
# Avoid preloading the user design into SRAM before this step: once the design
# is live, it can own the flash pins and complicate JTAG/sysCONFIG flash access.
#
# Falls back to OpenOCD jtagspi, then openFPGALoader.
flash: $(FLASH_PROGRAM_IMAGE)
	@if ecpdap -f $(ECPDAP_FREQ) flash unprotect && \
		ecpdap -f $(ECPDAP_FREQ) flash write $(FLASH_PROGRAM_IMAGE) && \
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
		    flash write_image erase unlock $(FLASH_PROGRAM_IMAGE) 0x0; \
		    exit"; then \
		echo "OpenOCD flash succeeded (fallback)."; \
	else \
		echo "ecpdap/OpenOCD failed, retrying with openFPGALoader..."; \
		openFPGALoader -b colorlight-i9 -f --verify $(FLASH_PROGRAM_IMAGE); \
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