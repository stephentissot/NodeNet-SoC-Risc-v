#!/usr/bin/env python3
import argparse
import pathlib
import struct
import zlib

FW_IMAGE_MAGIC = 0x46574E4E
FW_IMAGE_VERSION = 0x0001
FW_IMAGE_HEADER_SIZE = 64
FW_IMAGE_OFFSET = 64
FW_IMAGE_FORMAT = "<IHHIIIIIII28s"


def parse_u32(text: str) -> int:
    return int(text, 0)


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def build_header(payload: bytes, load_addr: int, entry_addr: int, flags: int) -> bytes:
    if struct.calcsize(FW_IMAGE_FORMAT) != FW_IMAGE_HEADER_SIZE:
        raise RuntimeError("FW_IMAGE_FORMAT size mismatch")

    image_size = len(payload)
    image_crc = crc32(payload)

    # header_crc32 is written as zero for checksum computation.
    header = struct.pack(
        FW_IMAGE_FORMAT,
        FW_IMAGE_MAGIC,
        FW_IMAGE_VERSION,
        FW_IMAGE_HEADER_SIZE,
        flags,
        load_addr,
        entry_addr,
        image_size,
        image_crc,
        0,
        FW_IMAGE_OFFSET,
        b"\x00" * 28,
    )

    header_crc = crc32(header)

    return struct.pack(
        FW_IMAGE_FORMAT,
        FW_IMAGE_MAGIC,
        FW_IMAGE_VERSION,
        FW_IMAGE_HEADER_SIZE,
        flags,
        load_addr,
        entry_addr,
        image_size,
        image_crc,
        header_crc,
        FW_IMAGE_OFFSET,
        b"\x00" * 28,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack raw firmware payload into stage0 image format")
    parser.add_argument("--input", required=True, help="Raw payload binary file")
    parser.add_argument("--output", required=True, help="Packed image output file")
    parser.add_argument("--load-addr", default="0x20000000", help="Runtime load address")
    parser.add_argument("--entry-addr", default="0x20000000", help="Runtime entry point")
    parser.add_argument("--flags", default="0", help="Image flags")
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)

    payload = input_path.read_bytes()
    load_addr = parse_u32(args.load_addr)
    entry_addr = parse_u32(args.entry_addr)
    flags = parse_u32(args.flags)

    header = build_header(payload, load_addr, entry_addr, flags)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(header + payload)

    print(f"[IMG] input: {input_path}")
    print(f"[IMG] output: {output_path}")
    print(f"[IMG] payload bytes: {len(payload)}")
    print(f"[IMG] load addr: 0x{load_addr:08X}")
    print(f"[IMG] entry addr: 0x{entry_addr:08X}")


if __name__ == "__main__":
    main()
