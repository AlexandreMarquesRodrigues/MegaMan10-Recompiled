#!/usr/bin/env python3
"""Uso: python tools/add_hle.py 0x8013c290 hle_OSFatal [0x... hle_outro ...]"""
import json, sys
from pathlib import Path

p = Path("games/megaman10/hle_registry.json")
reg = json.loads(p.read_text(encoding="utf-8"))
a = sys.argv[1:]
for addr, name in zip(a[0::2], a[1::2]):
    reg[addr.lower()] = name
    print(addr.lower(), "->", name)
p.write_text(json.dumps(reg, indent=2) + "\n", encoding="utf-8")
