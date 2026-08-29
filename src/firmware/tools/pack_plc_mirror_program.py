#!/usr/bin/env python3
import argparse
import pathlib
import struct

PLC_LINKED_PACKAGE_MAGIC = 0x31474B50
PLC_LINKED_PACKAGE_VERSION = 0x0001
PLC_LINKED_PACKAGE_HEADER_SIZE = 48
PLC_LINKED_PACKAGE_FORMAT = "<IHHIIIHHIIIIII"

OP_HALT = 0x00
OP_LOAD_POINT_BOOL = 0x10
OP_STORE_POINT_BOOL = 0x11


def parse_u32(text: str) -> int:
    return int(text, 0)


def parse_u16(text: str) -> int:
    value = int(text, 0)
    if value < 0 or value > 0xFFFF:
        raise argparse.ArgumentTypeError(f"u16 value out of range: {text}")
    return value


def checksum32(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def parse_pair(text: str) -> tuple[int, int]:
    parts = text.split(":", 1)
    if len(parts) != 2:
        raise argparse.ArgumentTypeError(f"invalid pair '{text}', expected INPUT:OUTPUT")
    return parse_u16(parts[0]), parse_u16(parts[1])


def build_payload(pairs: list[tuple[int, int]]) -> bytes:
    payload = bytearray()
    for input_index, output_index in pairs:
        payload.append(OP_LOAD_POINT_BOOL)
        payload.extend(struct.pack("<H", input_index))
        payload.append(OP_STORE_POINT_BOOL)
        payload.extend(struct.pack("<H", output_index))
    payload.append(OP_HALT)
    return bytes(payload)


def build_header(payload: bytes, pair_count: int, args: argparse.Namespace) -> bytes:
    if struct.calcsize(PLC_LINKED_PACKAGE_FORMAT) != PLC_LINKED_PACKAGE_HEADER_SIZE:
        raise RuntimeError("PLC_LINKED_PACKAGE_FORMAT size mismatch")

    symbol_count = parse_u32(args.symbol_count)
    relocation_count = parse_u32(args.relocation_count)
    if symbol_count == 0:
        symbol_count = pair_count * 2

    return struct.pack(
        PLC_LINKED_PACKAGE_FORMAT,
        PLC_LINKED_PACKAGE_MAGIC,
        PLC_LINKED_PACKAGE_VERSION,
        parse_u32(args.abi_version),
        parse_u32(args.flags),
        len(payload),
        parse_u32(args.entry_offset),
        symbol_count,
        relocation_count,
        parse_u32(args.max_instructions_per_scan),
        parse_u32(args.max_scan_time_us),
        parse_u32(args.runtime_header_addr),
        parse_u32(args.store_epoch),
        checksum32(payload),
        0,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a linked PLC package for the firmware mini-VM mirror program")
    parser.add_argument("--pair", action="append", required=True, type=parse_pair,
                        help="Mirror pair as INPUT_RUNTIME_INDEX:OUTPUT_RUNTIME_INDEX. Repeat for multiple pairs.")
    parser.add_argument("--output", required=True, help="Packed PLC package output file")
    parser.add_argument("--abi-version", default="1", help="Runtime ABI version")
    parser.add_argument("--flags", default="0", help="Package flags")
    parser.add_argument("--entry-offset", default="0", help="Entry point offset inside linked code")
    parser.add_argument("--symbol-count", default="0", help="Resolved symbol count; defaults to 2 per pair")
    parser.add_argument("--relocation-count", default="0", help="Resolved relocation count for metadata")
    parser.add_argument("--max-instructions-per-scan", default="16", help="Per-scan instruction budget")
    parser.add_argument("--max-scan-time-us", default="5000", help="Per-scan time budget")
    parser.add_argument("--runtime-header-addr", default="0x20100000", help="Expected runtime ABI header address")
    parser.add_argument("--store-epoch", default="1", help="Expected runtime store epoch")
    parser.add_argument("--emit-linked-code", default=None, help="Optional raw linked-code output path")
    args = parser.parse_args()

    payload = build_payload(args.pair)
    header = build_header(payload, len(args.pair), args)

    output_path = pathlib.Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(header + payload)

    if args.emit_linked_code is not None:
        linked_code_path = pathlib.Path(args.emit_linked_code)
        linked_code_path.parent.mkdir(parents=True, exist_ok=True)
        linked_code_path.write_bytes(payload)

    print(f"[PLCMIRROR] output: {output_path}")
    print(f"[PLCMIRROR] pairs: {len(args.pair)}")
    print(f"[PLCMIRROR] linked code bytes: {len(payload)}")
    print(f"[PLCMIRROR] runtime header addr: 0x{parse_u32(args.runtime_header_addr):08X}")
    print(f"[PLCMIRROR] store epoch: {parse_u32(args.store_epoch)}")
    for index, (input_index, output_index) in enumerate(args.pair, start=1):
        print(f"[PLCMIRROR] pair{index}: in={input_index} out={output_index}")


if __name__ == "__main__":
    main()