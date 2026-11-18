#!/usr/bin/env python3
"""Scan a raw Wii DOL for stores that may overlap an address and direct call xrefs."""
from __future__ import annotations

import argparse
import json
import struct
from collections import defaultdict
from pathlib import Path

SLOT = 0x8067B7E8
SDK_PREFIXES = ("DVD", "NAND", "IPC", "IOS_", "ES")
SDK_MODULE_RANGES = (
    (0x80145DC0, 0x8014E500),  # DVD library and private helpers
    (0x8017BBB0, 0x8017F700),  # ISFS/IPC/IOS library helpers
    (0x80180BC0, 0x80183680),  # NAND library and helpers
    (0x801D6000, 0x801D75D0),  # ES library and helpers
)


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from(">I", data, off)[0]


def s16(x: int) -> int:
    return x - 0x10000 if x & 0x8000 else x


def sign(x: int, bits: int) -> int:
    return x - (1 << bits) if x & (1 << (bits - 1)) else x


def section_list(dol: bytes):
    for i in range(7):
        off, va, size = u32(dol, i * 4), u32(dol, 0x48 + i * 4), u32(dol, 0x90 + i * 4)
        if size:
            yield i, off, va, size


def function_at(addr: int, funcs: list[dict]) -> dict | None:
    lo, hi = 0, len(funcs)
    while lo < hi:
        mid = (lo + hi) // 2
        if int(funcs[mid]["addr"], 16) <= addr:
            lo = mid + 1
        else:
            hi = mid
    if lo:
        f = funcs[lo - 1]
        start, size = int(f["addr"], 16), int(f["size"])
        if addr < start + size:
            return f
    return None


def store_width(ins: int) -> tuple[int, int] | None:
    op = ins >> 26
    dwidth = {36: 4, 37: 4, 38: 1, 39: 1, 44: 2, 45: 2, 47: 128, 52: 4, 54: 8}
    if op in dwidth:
        return dwidth[op], -1
    if op == 31:
        xo = (ins >> 1) & 0x3ff
        xwidth = {151: 4, 183: 4, 215: 1, 247: 1, 407: 2, 439: 2, 1014: 32}
        if xo in xwidth:
            return xwidth[xo], xo
    return None


def update_const(ins: int, regs: dict[int, int], segments=()) -> None:
    op = ins >> 26
    rd, ra, rb = (ins >> 21) & 31, (ins >> 16) & 31, (ins >> 11) & 31
    imm = ins & 0xffff
    if op == 15:  # addis / lis
        if ra == 0:
            regs[rd] = (s16(imm) << 16) & 0xffffffff
        elif ra in regs:
            regs[rd] = (regs[ra] + (s16(imm) << 16)) & 0xffffffff
        else:
            regs.pop(rd, None)
    elif op == 14:  # addi
        if ra == 0:
            regs[rd] = s16(imm) & 0xffffffff
        elif ra in regs:
            regs[rd] = (regs[ra] + s16(imm)) & 0xffffffff
        else:
            regs.pop(rd, None)
    elif op in (32, 33):  # lwz / lwzu from an initialized DOL section
        base = 0 if ra == 0 else regs.get(ra)
        ea = (base + s16(imm)) & 0xffffffff if base is not None else None
        value = None
        if ea is not None:
            for seg_va, seg_data in segments:
                offset = ea - seg_va
                if 0 <= offset <= len(seg_data) - 4:
                    value = struct.unpack_from(">I", seg_data, offset)[0]
                    break
        if value is None:
            regs.pop(rd, None)
        else:
            regs[rd] = value
        if op == 33 and ra != 0 and ea is not None:
            regs[ra] = ea
    elif op == 24:  # ori
        if rd in regs:
            regs[ra] = regs[rd] | imm
        else:
            regs.pop(ra, None)
    elif op == 25:  # oris
        if rd in regs:
            regs[ra] = regs[rd] | (imm << 16)
        else:
            regs.pop(ra, None)
    elif op == 31 and ((ins >> 1) & 0x3ff) == 444:  # mr/or rD,rS,rS
        if ra == rb and rb in regs:
            regs[rd] = regs[rb]
        else:
            regs.pop(rd, None)
    else:
        # Clobber a destination for common integer-producing instructions.
        if op not in (16, 17, 18, 19, 36, 37, 38, 39, 44, 45, 47, 52, 54):
            if op not in (24, 25):
                regs.pop(rd, None)


def scan(dol: bytes, funcs: list[dict], slot: int):
    insns: dict[int, int] = {}
    segments = []
    for _, off, va, size in section_list(dol):
        segments.append((va, dol[off:off + size]))
        for p in range(0, size - 3, 4):
            insns[va + p] = u32(dol, off + p)
    addresses = sorted(insns)
    writes, calls = [], []
    active_function = None
    regs: dict[int, int] = {}
    for addr in addresses:
        ins = insns[addr]
        f = function_at(addr, funcs)
        if not f:
            continue
        if active_function != int(f["addr"], 16):
            active_function = int(f["addr"], 16)
            regs = {13: 0x80680C40}  # runtime r13 at the observed stop
        op = ins >> 26
        info = store_width(ins)
        if info:
            width, xo = info
            ra = (ins >> 16) & 31
            if op == 31:
                rb = (ins >> 11) & 31
                base = 0 if ra == 0 else regs.get(ra)
                index = regs.get(rb)
                effective = (base + index) & 0xffffffff if base is not None and index is not None else None
            else:
                base = 0 if ra == 0 else regs.get(ra)
                effective = (base + s16(ins & 0xffff)) & 0xffffffff if base is not None else None
            if op == 47:
                width = (32 - ((ins >> 21) & 31)) * 4
            if effective is not None and effective < slot + 4 and effective + width > slot:
                writes.append((addr, f, effective, width))
            elif op == 31 and xo == 1014:
                # dcbz writes the 32-byte block addressed by a runtime pointer.
                writes.append((addr, f, None, width))
        if op == 18 and ins & 1:
            target = (addr + sign(ins & 0x03fffffc, 26)) & 0xffffffff if not (ins & 2) else sign(ins & 0x03fffffc, 26) & 0xffffffff
            calls.append((addr, f, target))
        update_const(ins, regs, segments)

    def context(addr: int):
        return [insns[a] for a in (addr - 8, addr - 4, addr, addr + 4, addr + 8) if a in insns]

    return insns, writes, calls, context


def disasm(ins: int) -> str:
    op = ins >> 26
    rt, ra = (ins >> 21) & 31, (ins >> 16) & 31
    d = s16(ins & 0xffff)
    if op in (36, 37, 38, 39, 44, 45, 47, 52, 54):
        names = {36:"stw",37:"stwu",38:"stb",39:"stbu",44:"sth",45:"sthu",47:"stmw",52:"stfs",54:"stfd"}
        return f"{names[op]} r{rt},{d}(r{ra})"
    if op == 15:
        return f"addis r{rt},r{ra},{s16(ins & 0xffff):#x}"
    if op == 14:
        return f"addi r{rt},r{ra},{d:#x}"
    if op == 24:
        return f"ori r{ra},r{rt},{ins & 0xffff:#x}"
    if op == 25:
        return f"oris r{ra},r{rt},{ins & 0xffff:#x}"
    if op == 31 and ((ins >> 1) & 0x3ff) == 1014:
        return f"dcbz r{(ins >> 16) & 31},r{(ins >> 11) & 31}"
    if op == 18 and ins & 1:
        return f"bl {(ins & 0x03fffffc):#x}"
    return f".long 0x{ins:08x}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dol", type=Path)
    ap.add_argument("func_list", type=Path)
    ap.add_argument("--slot", type=lambda x: int(x, 0), default=SLOT)
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    dol = args.dol.read_bytes()
    funcs = sorted(json.loads(args.func_list.read_text(encoding="utf-8")), key=lambda x: int(x["addr"], 16))
    insns, writes, calls, context = scan(dol, funcs, args.slot)
    lines = [f"DOL={args.dol} slot=0x{args.slot:08x}", "STATIC STORE CANDIDATES (effective write overlaps slot):"]
    if not writes:
        lines.append("  none found by supported address-formation/store patterns")
    for addr, f, effective, width in writes:
        eff = f"0x{effective:08x}" if effective is not None else "dynamic pointer argument"
        lines.append(f"  {addr:08x} {f['name']}@{f['addr']} effective={eff} width={width}")
        for a in (addr - 8, addr - 4, addr, addr + 4, addr + 8):
            i = insns.get(a)
            if i is None:
                continue
            lines.append(f"    {a:08x}: {disasm(i)}")
    by_target = defaultdict(list)
    for addr, caller, target in calls:
        by_target[target].append((addr, caller))
    name_by_addr = {int(f["addr"], 16): f for f in funcs}
    lines.append("\nDIRECT CALL XREFS:")
    for target in (0x801492a0, 0x8014a320):
        tf = name_by_addr.get(target, {"name": f"fn_{target:08X}", "addr": hex(target)})
        lines.append(f"  {tf['name']}@{target:08x}:")
        for addr, caller in by_target[target]:
            lines.append(f"    {caller['name']}@{caller['addr']} call@{addr:08x}")
    lines.append("\nCALLERS OUTSIDE DVD/IPC/IOS/NAND/ES TO NAMED SDK FUNCTIONS:")
    edges = []
    for addr, caller, target in calls:
        tf = name_by_addr.get(target)
        if not tf or not tf["name"].startswith(SDK_PREFIXES):
            continue
        caller_addr = int(caller["addr"], 16)
        if caller["name"].startswith(SDK_PREFIXES) or any(lo <= caller_addr < hi for lo, hi in SDK_MODULE_RANGES):
            continue
        edges.append((caller, tf, addr))
    for caller, callee, addr in sorted(edges, key=lambda x: (int(x[0]["addr"], 16), int(x[1]["addr"], 16))):
        lines.append(f"  {caller['name']}@{caller['addr']} -> {callee['name']}@{callee['addr']} call@{addr:08x}")
    lines.append(f"  total={len(edges)}")
    output = "\n".join(lines) + "\n"
    if args.output:
        args.output.write_text(output, encoding="utf-8")
    print(output, end="")


if __name__ == "__main__":
    main()
