from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: boot_hex_to_words.py <input.hex> <output.hex>", file=sys.stderr)
        return 2

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])

    tokens = []
    addr = 0

    for raw in src.read_text().splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("@"):
            addr = int(line[1:], 16)
            continue

        for tok in line.split():
            byte = int(tok, 16)
            if addr >= len(tokens):
                tokens.extend([0] * (addr - len(tokens) + 1))
            tokens[addr] = byte
            addr += 1

    while len(tokens) % 4:
        tokens.append(0)

    with dst.open("w", newline="\n") as handle:
        handle.write("@00000000\n")
        for index in range(0, len(tokens), 4):
            word = (
                tokens[index]
                | (tokens[index + 1] << 8)
                | (tokens[index + 2] << 16)
                | (tokens[index + 3] << 24)
            )
            handle.write(f"{word:08X}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())