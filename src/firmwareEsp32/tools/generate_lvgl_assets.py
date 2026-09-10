from __future__ import annotations

from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from typing import Iterable

import cairosvg
from PIL import Image, ImageOps


ROOT = Path(__file__).resolve().parents[1]
RESOURCES = ROOT / "resources"
INCLUDE_DIR = ROOT / "include"
SOURCE_DIR = ROOT / "src"
CANVAS_SIZE = (40, 40)


@dataclass(frozen=True)
class AssetSpec:
    input_name: str
    symbol_name: str


ASSETS: tuple[AssetSpec, ...] = (
    AssetSpec("LogoHD.png", "g_boot_logo_hd"),
    AssetSpec("bigsister.svg", "g_boot_bigsister"),
)


def load_image(resource_path: Path) -> Image.Image:
    if resource_path.suffix.lower() == ".svg":
        png_bytes = cairosvg.svg2png(url=str(resource_path))
        return Image.open(BytesIO(png_bytes)).convert("RGBA")
    return Image.open(resource_path).convert("RGBA")


def rasterize(resource_path: Path) -> tuple[int, int, bytes]:
    source = load_image(resource_path)
    fitted = ImageOps.contain(source, CANVAS_SIZE, method=Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", CANVAS_SIZE, (0, 0, 0, 255))
    offset_x = (CANVAS_SIZE[0] - fitted.width) // 2
    offset_y = (CANVAS_SIZE[1] - fitted.height) // 2
    canvas.alpha_composite(fitted, (offset_x, offset_y))

    rgb = canvas.convert("RGB")
    payload = bytearray()
    rgb_bytes = rgb.tobytes()
    for index in range(0, len(rgb_bytes), 3):
        red = rgb_bytes[index]
        green = rgb_bytes[index + 1]
        blue = rgb_bytes[index + 2]
        color565 = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        payload.append(color565 & 0xFF)
        payload.append((color565 >> 8) & 0xFF)
    return rgb.width, rgb.height, bytes(payload)


def format_byte_lines(data: bytes, per_line: int = 12) -> str:
    chunks: list[str] = []
    for start in range(0, len(data), per_line):
        row = ", ".join(f"0x{byte:02X}" for byte in data[start:start + per_line])
        chunks.append(f"    {row}")
    return ",\n".join(chunks)


def emit_header(specs: Iterable[AssetSpec]) -> str:
    lines = [
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
        "namespace boot_assets {",
        "",
    ]
    for spec in specs:
        lines.append(f"extern const lv_image_dsc_t {spec.symbol_name};")
    lines.extend([
        "",
        "} // namespace boot_assets",
        "",
    ])
    return "\n".join(lines)


def emit_source(entries: list[tuple[AssetSpec, int, int, bytes]]) -> str:
    lines = [
        '#include "boot_assets.h"',
        "",
        "namespace boot_assets {",
        "",
    ]
    for spec, width, height, payload in entries:
        lines.append(f"static const uint8_t {spec.symbol_name}_data[] = {{")
        lines.append(format_byte_lines(payload))
        lines.append("};")
        lines.append("")
        lines.append(f"const lv_image_dsc_t {spec.symbol_name} = {{")
        lines.append("    .header = {")
        lines.append("        .magic = LV_IMAGE_HEADER_MAGIC,")
        lines.append("        .cf = LV_COLOR_FORMAT_RGB565,")
        lines.append("        .flags = 0,")
        lines.append(f"        .w = {width},")
        lines.append(f"        .h = {height},")
        lines.append(f"        .stride = {width * 2},")
        lines.append("        .reserved_2 = 0,")
        lines.append("    },")
        lines.append(f"    .data_size = sizeof({spec.symbol_name}_data),")
        lines.append(f"    .data = {spec.symbol_name}_data,")
        lines.append("    .reserved = nullptr,")
        lines.append("    .reserved_2 = nullptr,")
        lines.append("};")
        lines.append("")
    lines.extend([
        "} // namespace boot_assets",
        "",
    ])
    return "\n".join(lines)


def main() -> None:
    rendered = []
    for spec in ASSETS:
        rendered.append((spec, *rasterize(RESOURCES / spec.input_name)))

    (INCLUDE_DIR / "boot_assets.h").write_text(emit_header(ASSETS), encoding="utf-8")
    (SOURCE_DIR / "boot_assets.cpp").write_text(emit_source(rendered), encoding="utf-8")


if __name__ == "__main__":
    main()