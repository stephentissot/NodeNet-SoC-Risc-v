DEVICE=45k
PACKAGE=CABGA381
SPEED=6

TOP=top

BUILD=build

LPF=constraints/colorlight_i9.lpf

FIRMWARE_HEX=src/firmware/build/nodenet_riscv.hex

# Recursive source discovery
rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))

SOURCES := $(call rwildcard,src/,*.sv) \
           $(call rwildcard,src/,*.v)


.PHONY: all firmware-build clean clean-firmware lock-flash unlock-flash

all: firmware-build $(BUILD)/$(TOP).bit

firmware-build:
	$(MAKE) -C src/firmware

clean-firmware:
	$(MAKE) -C src/firmware clean

clean: clean-firmware
	rm -rf $(BUILD)


$(BUILD):
	mkdir $(BUILD)


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


# Program FPGA configuration RAM
ram: $(BUILD)/$(TOP).svf
	openocd \
		-f interface/cmsis-dap.cfg \
		-f target/lattice-ecp5.cfg \
		-c "init; svf $<; exit"


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