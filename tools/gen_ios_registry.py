#!/usr/bin/env python3
"""Liga as funcoes IOS_* do func_list.json aos handlers de runtime/hle_ios.c no hle_registry.json."""
import json, re
from pathlib import Path

cfg = Path("games/megaman10")
funcs = json.loads((cfg / "func_list.json").read_text(encoding="utf-8"))
reg_path = cfg / "hle_registry.json"
reg = json.loads(reg_path.read_text(encoding="utf-8"))

WANT = ["IOS_Open", "IOS_Close", "IOS_Read", "IOS_Write", "IOS_Seek", "IOS_Ioctl", "IOS_Ioctlv",
        "IOS_OpenAsync", "IOS_CloseAsync", "IOS_ReadAsync", "IOS_WriteAsync", "IOS_SeekAsync",
        "IOS_IoctlAsync", "IOS_IoctlvAsync"]
by_name = {f["name"]: f for f in funcs}
added, missing = [], []
for n in WANT:
    f = by_name.get(n)
    if not f:
        missing.append(n)
        continue
    reg[f["addr"]] = "hle_" + n
    added.append((f["addr"], f["size"], n))
reg["_ios"] = "IOS_* sincronas completam na hora (runtime/hle_ios.c); variantes Async param com mensagem."
reg_path.write_text(json.dumps(reg, indent=2) + "\n", encoding="utf-8")

print("LIGADAS:")
for a, s, n in added:
    print(f"  {a} {s:5d} {n}")
print("AUSENTES no func_list:", ", ".join(missing) or "(nenhuma)")
print("OUTRAS funcoes IOS/IPC/ES (para a proxima rodada):")
for f in sorted((f for f in funcs if re.match(r"(?i)^(ios|ipc|esp|es_|isfs)", f["name"]) and f["name"] not in WANT),
                key=lambda f: int(f["addr"], 16)):
    print(f"  {f['addr']} {f['size']:5d} {f['name']}")