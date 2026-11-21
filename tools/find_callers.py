#!/usr/bin/env python3
"""Lista as funcoes que chamam diretamente um endereco: python tools/find_callers.py 0x8017db10"""
import json, re, sys
from pathlib import Path

target = int(sys.argv[1], 0)
funcs = {int(f["addr"], 16): f for f in json.loads(Path("games/megaman10/func_list.json").read_text(encoding="utf-8"))}
defn = re.compile(r"^void (func_[0-9a-f]{8})\(void\) \{")
call = re.compile(r"\bfunc_%08x\(\);" % target)
callers = {}
for p in sorted(Path("runtime/src/generated").glob("funcs_*.c")):
    cur = None
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        m = defn.match(line)
        if m:
            cur = int(m.group(1)[5:], 16)
        elif cur is not None and call.search(line):
            callers[cur] = callers.get(cur, 0) + 1
for a in sorted(callers):
    f = funcs.get(a, {})
    print(f"0x{a:08x} {f.get('size', '?'):>5} {f.get('name', '?')} (x{callers[a]})")
print(f"{len(callers)} chamadores diretos de 0x{target:08x}")