import json
from pathlib import Path

p = Path("games/megaman10/hle_registry.json")
reg = json.loads(p.read_text(encoding="utf-8"))
reg["0x8013de00"] = "hle_OSDisableInterrupts"
reg["0x8013de20"] = "hle_OSEnableInterrupts"
reg["0x8013de40"] = "hle_OSRestoreInterrupts"
p.write_text(json.dumps(reg, indent=2) + "\n", encoding="utf-8")
print("ok:", len(reg), "entradas")