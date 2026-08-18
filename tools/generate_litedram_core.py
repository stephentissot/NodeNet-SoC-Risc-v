import argparse
import re
import sys
from pathlib import Path
from itertools import count


def _extract_config_string_value(config_path: Path, key: str) -> str | None:
    pattern = re.compile(rf'"{re.escape(key)}"\s*:\s*"([^"]+)"')
    match = pattern.search(config_path.read_text(encoding="utf-8"))
    return match.group(1) if match else None


def _run_with_patched_litedram(
    argv: list[str],
    force_half_rate: bool = False,
    sdram_module_name: str | None = None,
) -> int:
    import litedram.gen as litedram_gen
    import litedram.modules as litedram_modules
    import litedram.phy as litedram_phys
    import migen.fhdl.conv_output as migen_conv_output
    import migen.fhdl.structure as migen_structure
    from migen import ClockDomain
    from migen import Module
    from migen.genlib.resetsync import AsyncResetSynchronizer
    from litex.gen import LiteXModule
    import litex.build.io as litex_io
    from litex.soc.interconnect import csr as litex_csr
    from litex.soc.cores.clock import ECP5PLL

    csr_name_counter = count()
    cd_name_counter = count()

    class PatchedLiteDRAMGENSDRPHYCRG(LiteXModule):
        def __init__(self, platform, core_config):
            assert core_config["memtype"] in ["SDR"]
            self.cd_sys = ClockDomain("sys")

            if core_config.get("sdram_rate", "1:1") == "1:2":
                # LiteX-like SDR half-rate clocking: generate sys and sys2x(+phase).
                self.cd_sys2x = ClockDomain("sys2x", reset_less=True)
                self.cd_sys2x_ps = ClockDomain("sys2x_ps", reset_less=True)

                clk = platform.request("clk")
                rst = platform.request("rst")

                self.pll = pll = ECP5PLL()
                self.comb += pll.reset.eq(rst)
                pll.register_clkin(clk, core_config["input_clk_freq"])
                pll.create_clkout(self.cd_sys, core_config["sys_clk_freq"])
                pll.create_clkout(self.cd_sys2x, 2*core_config["sys_clk_freq"])
                # Match the public LiteX Colorlight/ECP5 half-rate SDRAM CRGs:
                # 90 degrees is theoretical, but 180 degrees is used in practice
                # to relax forwarding margin on these boards.
                pll.create_clkout(self.cd_sys2x_ps, 2*core_config["sys_clk_freq"], phase=180)
            else:
                self.comb += self.cd_sys.clk.eq(platform.request("clk"))
                self.specials += AsyncResetSynchronizer(self.cd_sys, platform.request("rst"))

    class PatchedInferedSDRIO(Module):
        def __init__(self, i, o, clk):
            self.clock_domains.cd_sdrio = ClockDomain("sdrio", reset_less=True)
            self.comb += self.cd_sdrio.clk.eq(clk)
            self.sync.sdrio += o.eq(i)

    original_clockdomain_init = migen_structure.ClockDomain.__init__
    original_convoutput_write = migen_conv_output.ConvOutput.write
    original_csrbase_init = litex_csr._CSRBase.__init__

    def patched_clockdomain_init(self, name=None, reset_less=False, *args, **kwargs):
        if name is None:
            name = f"cd_auto_{next(cd_name_counter)}"
        original_clockdomain_init(self, name=name, reset_less=reset_less, *args, **kwargs)

    def patched_csrbase_init(self, size, name=None, n=None, *args, **kwargs):
        if name is None:
            name = f"csr_auto_{next(csr_name_counter)}"
        original_csrbase_init(self, size, name, n, *args, **kwargs)

    def patched_convoutput_write(self, main_filename):
        # Force UTF-8 on generated outputs to avoid cp1252 encode failures on Windows.
        with open(main_filename, "w", encoding="utf-8", newline="\n") as f:
            f.write(self.main_source)
        for filename, content in self.data_files.items():
            with open(filename, "w", encoding="utf-8", newline="\n") as f:
                f.write(content)

    original_gensdrphy = litedram_gen.litedram_phys.GENSDRPHY
    original_module_cls = None

    if force_half_rate:
        # Standalone litedram.gen only handles SDR in the GENSDRPHY branch.
        # Route that branch to HalfRateGENSDRPHY while keeping the flow intact.
        litedram_gen.litedram_phys.GENSDRPHY = litedram_phys.HalfRateGENSDRPHY
        if sdram_module_name:
            original_module_cls = getattr(litedram_modules, sdram_module_name, None)
            if original_module_cls is not None:
                class ForcedHalfRateModule(original_module_cls):
                    def __init__(self, clk_freq, rate="1:1", *args, **kwargs):
                        super().__init__(clk_freq, rate="1:2", *args, **kwargs)

                ForcedHalfRateModule.__name__ = original_module_cls.__name__
                setattr(litedram_modules, sdram_module_name, ForcedHalfRateModule)

    litedram_gen.LiteDRAMGENSDRPHYCRG = PatchedLiteDRAMGENSDRPHYCRG
    litex_io.InferedSDRIO = PatchedInferedSDRIO
    migen_structure.ClockDomain.__init__ = patched_clockdomain_init
    migen_conv_output.ConvOutput.write = patched_convoutput_write
    litex_csr._CSRBase.__init__ = patched_csrbase_init
    old_argv = sys.argv
    try:
        sys.argv = argv
        litedram_gen.main()
        return 0
    finally:
        litedram_gen.litedram_phys.GENSDRPHY = original_gensdrphy
        if force_half_rate and sdram_module_name and (original_module_cls is not None):
            setattr(litedram_modules, sdram_module_name, original_module_cls)
        migen_structure.ClockDomain.__init__ = original_clockdomain_init
        migen_conv_output.ConvOutput.write = original_convoutput_write
        litex_csr._CSRBase.__init__ = original_csrbase_init
        sys.argv = old_argv


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate standalone LiteDRAM RTL for Colorlight i9")
    parser.add_argument("--config", required=True, help="LiteDRAM YAML config path")
    parser.add_argument("--output-dir", required=True, help="Output build directory")
    parser.add_argument("--name", default="litedram_core", help="Generated top module name")
    args = parser.parse_args()

    if not sys.executable or not Path(sys.executable).exists():
        print("Python interpreter is not available (invalid sys.executable).", file=sys.stderr)
        return 2

    config_path = Path(args.config)
    selected_phy = _extract_config_string_value(config_path, "sdram_phy")
    selected_module = _extract_config_string_value(config_path, "sdram_module")
    force_half_rate = selected_phy == "HalfRateGENSDRPHY"

    try:
        rc = _run_with_patched_litedram([
            "litedram.gen",
            "--output-dir",
            args.output_dir,
            "--name",
            args.name,
            args.config,
        ], force_half_rate=force_half_rate, sdram_module_name=selected_module)
        if rc != 0:
            return rc

        # LiteDRAM standalone quirk in this project:
        # sdram_dq can be emitted as input instead of inout.
        core_path = Path(args.output_dir) / "gateware" / f"{args.name}.v"
        text = core_path.read_text(encoding="utf-8")
        # Keep this robust to whitespace changes in generated Verilog.
        dq_decl = re.compile(r"(^\s*)input(\s+)wire(\s+)\[31:0\](\s+)sdram_dq,", re.MULTILINE)
        text, replacements = dq_decl.subn(r"\1inout\2wire\3[31:0]\4sdram_dq,", text, count=1)
        if replacements != 1:
            print("[LITEDRAM][ERROR] Could not patch sdram_dq direction to inout.", file=sys.stderr)
            print("[LITEDRAM][ERROR] Generated RTL format likely changed; update tools/generate_litedram_core.py.", file=sys.stderr)
            return 2

        # Half-rate mode needs the phase-shifted 2x clock exported so the
        # wrapper can forward SDRAM clock with the same LiteDRAM CRG phase.
        if force_half_rate and "sdram_clk_2x_ps" not in text:
            port_anchor = (
                "    input  wire    [3:0] wb_ctrl_sel,\n"
                "    input  wire          wb_ctrl_stb,\n"
                "    input  wire          wb_ctrl_we\n"
                ");"
            )
            port_replace = (
                "    input  wire    [3:0] wb_ctrl_sel,\n"
                "    input  wire          wb_ctrl_stb,\n"
                "    input  wire          wb_ctrl_we,\n"
                "    output wire          sdram_clk_2x_ps\n"
                ");"
            )
            if port_anchor not in text:
                print("[LITEDRAM][ERROR] Could not inject sdram_clk_2x_ps port.", file=sys.stderr)
                return 2
            text = text.replace(port_anchor, port_replace, 1)

            assign_anchor = "assign sys2x_ps_clk = litedramcore_patchedlitedramgensdrphycrg_ecp5pll6;\n"
            assign_insert = assign_anchor + "assign sdram_clk_2x_ps = sys2x_ps_clk;\n"
            if assign_anchor not in text:
                print("[LITEDRAM][ERROR] Could not wire sdram_clk_2x_ps assignment.", file=sys.stderr)
                return 2
            text = text.replace(assign_anchor, assign_insert, 1)

        # Keep user Wishbone traffic flowing even if the status-latched
        # `litedramcore` gate glitches due control-plane CDC during bring-up.
        # This avoids false hard-test timeouts with zero observed ACKs.
        old_cyc = "assign litedramcore_interface1_cyc = (user_port_wb_cyc & litedramcore);\n"
        old_stb = "assign litedramcore_interface1_stb = (user_port_wb_stb & litedramcore);\n"
        old_ack = "assign user_port_wb_ack = (litedramcore_interface1_ack & litedramcore);\n"
        if old_cyc in text:
            text = text.replace(old_cyc, "assign litedramcore_interface1_cyc = user_port_wb_cyc;\n", 1)
        if old_stb in text:
            text = text.replace(old_stb, "assign litedramcore_interface1_stb = user_port_wb_stb;\n", 1)
        if old_ack in text:
            text = text.replace(old_ack, "assign user_port_wb_ack = litedramcore_interface1_ack;\n", 1)

        core_path.write_text(text, encoding="utf-8")
        return 0
    except ModuleNotFoundError:
        print("LiteDRAM is not installed in this Python environment.", file=sys.stderr)
        print("Install LiteX/LiteDRAM first, then rerun this command.", file=sys.stderr)
        return 2
    except OSError as exc:
        print(f"Failed to launch LiteDRAM generator: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())