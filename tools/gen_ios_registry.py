#!/usr/bin/env python3
"""Liga as funcoes IOS ao hle_registry.json (por nome e por endereco) e confere o comando IPC.
Rode ANTES de regerar a traducao (depois disso as funcoes viram thunks)."""
import json, re
from pathlib import Path

cfg = Path("games/megaman10")
gen = Path("runtime/src/generated")
funcs = json.loads((cfg / "func_list.json").read_text(encoding="utf-8"))
by_name = {f["name"]: f for f in funcs}
reg_path = cfg / "hle_registry.json"
reg = json.loads(reg_path.read_text(encoding="utf-8"))

for n in ["IOS_Open", "IOS_OpenAsync", "IOS_IoctlAsync", "IOS_Ioctlv"]:
    f = by_name.get(n)
    if f:
        reg[f["addr"]] = "hle_" + n

GUESS = [
    (0x8017dfa0, "IOS_CloseAsync", 2), (0x8017e060, "IOS_Close", 2),
    (0x8017e110, "IOS_ReadAsync", 3),  (0x8017e210, "IOS_Read", 3),
    (0x8017e320, "IOS_WriteAsync", 4), (0x8017e420, "IOS_Write", 4),
    (0x8017e530, "IOS_SeekAsync", 5),  (0x8017e610, "IOS_Seek", 5),
    (0x8017e840, "IOS_Ioctl", 6),      (0x8017eab0, "IOS_IoctlvAsync", 7),
]

def body(addr):
    start = re.compile(r"^void func_%08x\(void\) \{" % addr)
    for p in sorted(gen.glob("funcs_*.c")):
        lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
        for i, l in enumerate(lines):
            if start.match(l):
                j = i + 1
                while j < len(lines) and not lines[j].startswith("// ---- "):
                    j += 1
                return lines[i:j]
    return []

imm = re.compile(r"\(uint32_t\)\(int32_t\)\((-?0x[0-9a-fA-F]+)\)")
print("CONFERENCIA (comando IPC esperado dentro de cada funcao):")
for addr, name, cmd in GUESS:
    vals = []
    for l in body(addr):
        for m in imm.finditer(l):
            v = int(m.group(1), 16)
            if v not in vals:
                vals.append(v)
    ok = "OK" if cmd in vals else "CONFERIR"
    shown = ", ".join(hex(v) for v in vals[:10])
    print(f"  0x{addr:08x} {name:16s} esperado={cmd}  imediatos=[{shown}]  {ok}")
    reg[f"0x{addr:08x}"] = "hle_" + name

reg["_ios"] = "IOS_* sincronas completam na hora (runtime/hle_ios.c); variantes Async param com mensagem."
reg_path.write_text(json.dumps(reg, indent=2) + "\n", encoding="utf-8")
print("hle_registry.json atualizado.")