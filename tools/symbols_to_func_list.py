#!/usr/bin/env python3
"""Build WiiRecomp func_list.json from a DTK symbols.txt file."""

import argparse
import json
import re
from pathlib import Path

SYMBOL_RE = re.compile(
    r"^\s*(?P<name>[^=\s]+)\s*=\s*[^:;]+:(?P<addr>0x[0-9A-Fa-f]+);"
    r"\s*//\s*type:function\s+size:(?P<size>0x[0-9A-Fa-f]+)(?:\s|$)"
)


def convert(source: Path) -> list[dict]:
    functions = []
    for line_number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), 1):
        match = SYMBOL_RE.match(line)
        if not match:
            continue
        functions.append({
            "addr": f"0x{int(match['addr'], 16):08x}",
            "size": int(match["size"], 16),
            "name": match["name"],
            "origin": "elf-symbol",
        })
    functions.sort(key=lambda fn: int(fn["addr"], 16))
    return functions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("symbols", type=Path, help="DTK symbols.txt")
    parser.add_argument("output", type=Path, nargs="?", default=Path(__file__).resolve().parents[1] / "games/megaman10/func_list.json")
    args = parser.parse_args()
    functions = convert(args.symbols)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(functions, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(functions)} functions to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

