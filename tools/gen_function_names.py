#!/usr/bin/env python3
"""Generate the address-to-symbol map used by the native runtime."""
import json
import pathlib
import sys


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def main() -> int:
    source, target = map(pathlib.Path, sys.argv[1:3])
    functions = json.loads(source.read_text(encoding="utf-8"))
    rows = sorted((int(item["addr"], 0), item["name"])
                  for item in functions if "addr" in item and "name" in item)
    lines = [
        "/* Generated from games/megaman10/func_list.json; do not edit. */",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "typedef struct { uint32_t addr; const char *name; } FunctionName;",
        "static const FunctionName k_function_names[] = {",
    ]
    lines.extend(f"    {{ 0x{addr:08x}u, {c_string(name)} }}," for addr, name in rows)
    lines.extend([
        "};",
        "const char *ppc_function_name(uint32_t addr) {",
        "    size_t lo = 0, hi = sizeof(k_function_names) / sizeof(k_function_names[0]);",
        "    while (lo < hi) {",
        "        size_t mid = lo + (hi - lo) / 2;",
        "        if (k_function_names[mid].addr < addr) lo = mid + 1; else hi = mid;",
        "    }",
        "    return lo < sizeof(k_function_names) / sizeof(k_function_names[0]) &&",
        "           k_function_names[lo].addr == addr ? k_function_names[lo].name : NULL;",
        "}",
        "",
    ])
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
