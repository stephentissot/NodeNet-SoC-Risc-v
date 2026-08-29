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


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify direct-flash PLC linked package header and checksum")
    parser.add_argument("--input", required=True, help="Packed PLC package file")
    parser.add_argument("--slot-size", default="0x20000", help="Maximum allowed package size")
    parser.add_argument("--expect-runtime-header-addr", default=None, help="Optional expected runtime ABI header address")
    parser.add_argument("--expect-store-epoch", default=None, help="Optional expected runtime store epoch")
    args = parser.parse_args()

    package_path = pathlib.Path(args.input)
    data = package_path.read_bytes()
    if len(data) < PLC_LINKED_PACKAGE_HEADER_SIZE:
        raise SystemExit("[PLCPKG][ERROR] file too small to contain header")

    if len(data) > parse_u32(args.slot_size):
        raise SystemExit("[PLCPKG][ERROR] package exceeds reserved PLC flash slot")

    fields = struct.unpack(PLC_LINKED_PACKAGE_FORMAT, data[:PLC_LINKED_PACKAGE_HEADER_SIZE])
    (
        magic,
        version,
        abi_version,
        flags,
        code_size,
        entry_offset,
        symbol_count,
        relocation_count,
        max_instructions_per_scan,
        max_scan_time_us,
        runtime_header_addr,
        store_epoch,
        linked_code_checksum,
        reserved0,
    ) = fields

    if magic != PLC_LINKED_PACKAGE_MAGIC:
        raise SystemExit(f"[PLCPKG][ERROR] bad magic: 0x{magic:08X}")
    if version != PLC_LINKED_PACKAGE_VERSION:
        raise SystemExit(f"[PLCPKG][ERROR] bad version: {version}")
    if reserved0 != 0:
        raise SystemExit(f"[PLCPKG][ERROR] reserved0 must be zero, got 0x{reserved0:08X}")
    if entry_offset > code_size:
        raise SystemExit("[PLCPKG][ERROR] entry offset outside linked code")
    if PLC_LINKED_PACKAGE_HEADER_SIZE + code_size != len(data):
        raise SystemExit("[PLCPKG][ERROR] file size does not match header code_size")

    payload = data[PLC_LINKED_PACKAGE_HEADER_SIZE:]
    calc_checksum = checksum32(payload)
    if calc_checksum != linked_code_checksum:
        raise SystemExit(
            f"[PLCPKG][ERROR] checksum mismatch: got 0x{linked_code_checksum:08X}, expected 0x{calc_checksum:08X}"
        )

    if args.expect_runtime_header_addr is not None:
        expected_runtime_header_addr = parse_u32(args.expect_runtime_header_addr)
        if runtime_header_addr != expected_runtime_header_addr:
            raise SystemExit(
                f"[PLCPKG][ERROR] runtime header addr mismatch: got 0x{runtime_header_addr:08X}, expected 0x{expected_runtime_header_addr:08X}"
            )

    if args.expect_store_epoch is not None:
        expected_store_epoch = parse_u32(args.expect_store_epoch)
        if store_epoch != expected_store_epoch:
            raise SystemExit(
                f"[PLCPKG][ERROR] store epoch mismatch: got {store_epoch}, expected {expected_store_epoch}"
            )

    print(f"[PLCPKG] file: {package_path}")
    print(f"[PLCPKG] abi version: {abi_version}")
    print(f"[PLCPKG] flags: 0x{flags:08X}")
    print(f"[PLCPKG] linked code bytes: {code_size}")
    print(f"[PLCPKG] entry offset: {entry_offset}")
    print(f"[PLCPKG] symbol count: {symbol_count}")
    print(f"[PLCPKG] relocation count: {relocation_count}")
    print(f"[PLCPKG] max instructions per scan: {max_instructions_per_scan}")
    print(f"[PLCPKG] max scan time us: {max_scan_time_us}")
    print(f"[PLCPKG] runtime header addr: 0x{runtime_header_addr:08X}")
    print(f"[PLCPKG] store epoch: {store_epoch}")
    print(f"[PLCPKG] checksum32: 0x{linked_code_checksum:08X}")
    print("[PLCPKG] verify: OK")


if __name__ == "__main__":
    main()