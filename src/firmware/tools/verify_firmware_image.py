#!/usr/bin/env python3
import argparse
import pathlib
import struct
import zlib

FW_IMAGE_MAGIC = 0x46574E4E
FW_IMAGE_VERSION = 0x0001
FW_IMAGE_HEADER_SIZE = 64
FW_IMAGE_FORMAT = "<IHHIIIIIII28s"


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_u32(text: str) -> int:
    return int(text, 0)


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify stage0 firmware image header and payload CRC")
    parser.add_argument("--input", required=True, help="Packed image file (*.img)")
    parser.add_argument("--sdram-base", default="0x20000000", help="Allowed SDRAM base")
    parser.add_argument("--sdram-size", default="0x00800000", help="Allowed SDRAM size")
    args = parser.parse_args()

    image_path = pathlib.Path(args.input)
    data = image_path.read_bytes()

    if len(data) < FW_IMAGE_HEADER_SIZE:
        raise SystemExit("[IMG][ERROR] file too small to contain header")

    header_bytes = data[:FW_IMAGE_HEADER_SIZE]
    fields = struct.unpack(FW_IMAGE_FORMAT, header_bytes)

    (
        magic,
        header_version,
        header_size,
        flags,
        load_addr,
        entry_addr,
        image_size,
        image_crc32,
        header_crc32,
        image_offset,
        _reserved,
    ) = fields

    if magic != FW_IMAGE_MAGIC:
        raise SystemExit(f"[IMG][ERROR] bad magic: 0x{magic:08X}")
    if header_version != FW_IMAGE_VERSION:
        raise SystemExit(f"[IMG][ERROR] bad version: {header_version}")
    if header_size != FW_IMAGE_HEADER_SIZE:
        raise SystemExit(f"[IMG][ERROR] bad header size: {header_size}")
    if image_offset < header_size:
        raise SystemExit(f"[IMG][ERROR] image_offset < header_size ({image_offset} < {header_size})")

    hdr_for_crc = bytearray(header_bytes)
    header_crc_offset = struct.calcsize("<IHHIIIII")
    hdr_for_crc[header_crc_offset:header_crc_offset + 4] = b"\x00\x00\x00\x00"
    calc_header_crc = crc32(bytes(hdr_for_crc))
    if calc_header_crc != header_crc32:
        raise SystemExit(
            f"[IMG][ERROR] header CRC mismatch: got 0x{header_crc32:08X}, expected 0x{calc_header_crc:08X}"
        )

    if image_offset + image_size > len(data):
        raise SystemExit("[IMG][ERROR] payload extends beyond file size")

    payload = data[image_offset:image_offset + image_size]
    calc_payload_crc = crc32(payload)
    if calc_payload_crc != image_crc32:
        raise SystemExit(
            f"[IMG][ERROR] payload CRC mismatch: got 0x{image_crc32:08X}, expected 0x{calc_payload_crc:08X}"
        )

    sdram_base = parse_u32(args.sdram_base)
    sdram_size = parse_u32(args.sdram_size)
    sdram_end = sdram_base + sdram_size
    load_end = load_addr + image_size

    if load_addr < sdram_base or load_end > sdram_end or load_end < load_addr:
        raise SystemExit(
            f"[IMG][ERROR] load range 0x{load_addr:08X}-0x{load_end:08X} outside SDRAM"
        )

    if entry_addr < load_addr or entry_addr >= load_end:
        raise SystemExit(
            f"[IMG][ERROR] entry 0x{entry_addr:08X} outside load range"
        )

    print(f"[IMG] file: {image_path}")
    print(f"[IMG] flags: 0x{flags:08X}")
    print(f"[IMG] load addr: 0x{load_addr:08X}")
    print(f"[IMG] entry addr: 0x{entry_addr:08X}")
    print(f"[IMG] payload bytes: {image_size}")
    print(f"[IMG] payload crc32: 0x{image_crc32:08X}")
    print("[IMG] verify: OK")


if __name__ == "__main__":
    main()
