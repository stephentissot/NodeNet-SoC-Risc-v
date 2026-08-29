#!/usr/bin/env python3
import argparse
import pathlib
import struct

PLC_LINKED_PACKAGE_MAGIC = 0x31474B50
PLC_LINKED_PACKAGE_VERSION = 0x0001
PLC_LINKED_PACKAGE_HEADER_SIZE = 48
PLC_LINKED_PACKAGE_FORMAT = "<IHHIIIHHIIIIII"


def parse_u32(text: str) -> int:
    return int(text, 0)


def checksum32(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def build_header(payload: bytes, args: argparse.Namespace) -> bytes:
    if struct.calcsize(PLC_LINKED_PACKAGE_FORMAT) != PLC_LINKED_PACKAGE_HEADER_SIZE:
        raise RuntimeError("PLC_LINKED_PACKAGE_FORMAT size mismatch")

    return struct.pack(
        PLC_LINKED_PACKAGE_FORMAT,
        PLC_LINKED_PACKAGE_MAGIC,
        PLC_LINKED_PACKAGE_VERSION,
        parse_u32(args.abi_version),
        parse_u32(args.flags),
        len(payload),
        parse_u32(args.entry_offset),
        parse_u32(args.symbol_count),
        parse_u32(args.relocation_count),
        parse_u32(args.max_instructions_per_scan),
        parse_u32(args.max_scan_time_us),
        parse_u32(args.runtime_header_addr),
        parse_u32(args.store_epoch),
        checksum32(payload),
        0,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack raw linked PLC code into direct-flash package format")
    parser.add_argument("--input", required=True, help="Raw linked PLC code binary file")
    parser.add_argument("--output", required=True, help="Packed PLC package output file")
    parser.add_argument("--abi-version", default="1", help="Runtime ABI version")
    parser.add_argument("--flags", default="0", help="Package flags")
    parser.add_argument("--entry-offset", default="0", help="Entry point offset inside linked code")
    parser.add_argument("--symbol-count", default="0", help="Resolved symbol count")
    parser.add_argument("--relocation-count", default="0", help="Resolved relocation count")
    parser.add_argument("--max-instructions-per-scan", default="200", help="Per-scan instruction budget")
    parser.add_argument("--max-scan-time-us", default="10000", help="Per-scan time budget")
    parser.add_argument("--runtime-header-addr", default="0x20100000", help="Expected runtime ABI header address")
    parser.add_argument("--store-epoch", default="1", help="Expected runtime store epoch")
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)
    payload = input_path.read_bytes()

    header = build_header(payload, args)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(header + payload)

    print(f"[PLCPKG] input: {input_path}")
    print(f"[PLCPKG] output: {output_path}")
    print(f"[PLCPKG] linked code bytes: {len(payload)}")
    print(f"[PLCPKG] abi version: {parse_u32(args.abi_version)}")
    print(f"[PLCPKG] runtime header addr: 0x{parse_u32(args.runtime_header_addr):08X}")
    print(f"[PLCPKG] store epoch: {parse_u32(args.store_epoch)}")


if __name__ == "__main__":
    main()