#!/usr/bin/env python3
"""Summarize named SDK functions in a WiiRecomp func_list.json."""

from __future__ import annotations

import argparse
import json
import re
from collections import OrderedDict
from pathlib import Path

PREFIXES = ("OS", "EXI", "SI", "VI", "GX", "AX", "AI", "DVD", "PAD",
            "KPAD", "WPAD", "IPC", "IOS", "NAND", "ARC", "MEM", "CARD")


def category(name: str) -> str:
    bare = name.lstrip("_")
    # `exit` is a C runtime symbol, not an EXI SDK symbol.
    if bare.lower() == "exit":
        return "outros"
    upper = bare.upper()
    for prefix in PREFIXES:
        if upper.startswith(prefix):
            return prefix
    return "outros"


def read_executed_names(log_path: Path | None) -> set[str]:
    if log_path is None or not log_path.exists():
        return set()
    text = log_path.read_text(encoding="utf-8", errors="replace")
    names = set(re.findall(r"(?:function~|boot_sdk_function\s+)([A-Za-z_][A-Za-z0-9_]*)", text))
    names.update(re.findall(r"HLE_CALL\s+([A-Za-z_][A-Za-z0-9_]*)", text))
    names.update(re.findall(r"^\s+([A-Za-z_][A-Za-z0-9_]*)\s+@\s+[0-9a-fA-F]{8}\s*$", text, re.MULTILINE))
    names.update(re.findall(r"^\s+function:\s+([A-Za-z_][A-Za-z0-9_]*)", text, re.MULTILINE))
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("func_list", nargs="?", type=Path,
                        default=Path("games/megaman10/func_list.json"))
    parser.add_argument("--boot-log", type=Path,
                        help="optional runtime log used to mark observed boot functions")
    args = parser.parse_args()

    funcs = json.loads(args.func_list.read_text(encoding="utf-8"))
    groups: OrderedDict[str, list] = OrderedDict((p, []) for p in PREFIXES)
    groups["outros"] = []
    for fn in funcs:
        name = fn.get("name", "")
        if not name or re.fullmatch(r"fn_[0-9a-fA-F]{8}", name):
            continue
        groups[category(name)].append(fn)

    executed = {name.lstrip("_").upper() for name in read_executed_names(args.boot_log)}
    print(f"{'Prefixo':<8} {'Funções':>8} {'Bytes':>12} {'Boot visto':>12}")
    for prefix, entries in groups.items():
        boot = sum(1 for fn in entries if fn["name"].lstrip("_").upper() in executed)
        size = sum(int(fn.get("size", 0)) for fn in entries)
        print(f"{prefix:<8} {len(entries):>8} {size:>12} {boot:>12}")
    print(f"{'TOTAL':<8} {sum(map(len, groups.values())):>8} "
          f"{sum(sum(int(f.get('size', 0)) for f in v) for v in groups.values()):>12} "
          f"{sum(1 for v in groups.values() for f in v if f['name'].lstrip('_').upper() in executed):>12}")
    if not args.boot_log:
        print("Boot visto is zero because --boot-log was not supplied.")
    else:
        seen = [fn for entries in groups.values() for fn in entries
                if fn["name"].lstrip("_").upper() in executed]
        print("Boot-observed functions: " + (", ".join(
            f"{fn['name']}@{fn['addr']}" for fn in sorted(seen, key=lambda x: int(x["addr"], 16)))
            or "none"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
