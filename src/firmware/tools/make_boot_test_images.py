#!/usr/bin/env python3
import argparse
import pathlib
import struct
import zlib

FW_IMAGE_FORMAT = "<IHHIIIIIII28s"
FW_HEADER_SIZE = 64


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_header(blob: bytes):
    if len(blob) < FW_HEADER_SIZE:
        raise ValueError("input image too small")
    return list(struct.unpack(FW_IMAGE_FORMAT, blob[:FW_HEADER_SIZE]))


def write_header(fields) -> bytes:
    return struct.pack(FW_IMAGE_FORMAT, *fields)


def with_recomputed_header_crc(fields):
    fields = list(fields)
    fields[8] = 0
    tmp = write_header(fields)
    fields[8] = crc32(tmp)
    return fields


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate corrupted stage0 test images from a valid .img")
    parser.add_argument("--input", required=True, help="Valid packed image")
    parser.add_argument("--out-dir", required=True, help="Directory for generated test images")
    parser.add_argument("--prefix", default="nodenet_riscv_app", help="Output filename prefix")
    args = parser.parse_args()

    src = pathlib.Path(args.input)
    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    data = bytearray(src.read_bytes())
    fields = parse_header(data)

    # CRC-invalid payload image: flip first payload byte, keep original header.
    crc_bad = bytearray(data)
    image_offset = fields[9]
    image_size = fields[6]
    if image_size > 0 and image_offset < len(crc_bad):
        crc_bad[image_offset] ^= 0x01
    (out_dir / f"{args.prefix}.bad_crc.img").write_bytes(crc_bad)

    # Size-invalid image: set load size outside SDRAM bounds and recompute header CRC.
    size_bad = bytearray(data)
    fields_size = list(fields)
    fields_size[6] = 0x00900000  # 9 MB, outside 8 MB SDRAM window
    fields_size = with_recomputed_header_crc(fields_size)
    size_bad[:FW_HEADER_SIZE] = write_header(fields_size)
    (out_dir / f"{args.prefix}.bad_size.img").write_bytes(size_bad)

    # Absent image simulation: erased flash sector pattern (all 0xFF bytes).
    missing = bytes([0xFF] * 4096)
    (out_dir / f"{args.prefix}.missing.img").write_bytes(missing)

    print(f"[IMGTEST] source: {src}")
    print(f"[IMGTEST] out dir: {out_dir}")
    print(f"[IMGTEST] generated: {args.prefix}.bad_crc.img")
    print(f"[IMGTEST] generated: {args.prefix}.bad_size.img")
    print(f"[IMGTEST] generated: {args.prefix}.missing.img")


if __name__ == "__main__":
    main()
