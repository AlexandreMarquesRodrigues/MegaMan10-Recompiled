#!/usr/bin/env python3
"""Find static PowerPC data accesses to the MM10 DOL's MMIO ranges.

Usage: python tools/find_hw.py private/main.dol --func-list games/megaman10/func_list.json
The scanner tracks common lis/addis + addi/ori address construction patterns.
"""
from __future__ import annotations
import argparse, json, struct
from collections import defaultdict
from pathlib import Path

BLOCKS = [
    ("CP", 0xCC000000, 0xCC001000), ("PE", 0xCC001000, 0xCC002000),
    ("VI", 0xCC002000, 0xCC003000), ("PI", 0xCC003000, 0xCC004000),
    ("DSP", 0xCC005000, 0xCC006000), ("AI", 0xCC006C00, 0xCC007000),
    ("GX FIFO", 0xCC008000, 0xCC009000), ("DI", 0xCD006000, 0xCD006400),
    ("SI", 0xCD006400, 0xCD006800), ("EXI", 0xCD006800, 0xCD007000),
]

def sx(v: int, bits: int) -> int:
    return (v ^ (1 << (bits-1))) - (1 << (bits-1))

def dol_sections(data: bytes):
    for i in range(18):
        off = struct.unpack_from(">I", data, i*4)[0]
        if not off: continue
        va = struct.unpack_from(">I", data, 0x48+i*4)[0]
        size = struct.unpack_from(">I", data, 0x90+i*4)[0]
        if size and off+size <= len(data): yield off, va, size

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dol", type=Path); ap.add_argument("--func-list", type=Path, default=Path("games/megaman10/func_list.json"))
    a=ap.parse_args(); data=a.dol.read_bytes()
    funcs=sorted(((int(f["addr"],16), int(f.get("size",0)), f.get("name", "")) for f in json.loads(a.func_list.read_text())), key=lambda x:x[0])
    found=defaultdict(set)
    for off, va, size in dol_sections(data):
        end=va+size
        for start, fsize, name in funcs:
            lo=max(start,va); hi=min(start+fsize,end)
            if not name or hi-lo<4: continue
            regs=[None]*32
            for pc in range(lo,hi-3,4):
                ins=struct.unpack_from(">I",data,off+pc-va)[0]; op=ins>>26; rt=(ins>>21)&31; ra=(ins>>16)&31; imm=ins&0xffff
                if op==15: # lis / addis
                    base=regs[ra] if ra else 0
                    regs[rt]=None if base is None else (base + (sx(imm,16)<<16)) & 0xffffffff
                elif op==14: # addi
                    base=regs[ra] if ra else 0
                    regs[rt]=None if base is None else (base+sx(imm,16))&0xffffffff
                elif op==24: # ori
                    regs[ra]=None if regs[rt] is None else (regs[rt]|imm)&0xffffffff
                elif op in (32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47):
                    base=regs[ra] if ra else 0
                    if base is not None:
                        addr=(base+sx(imm,16))&0xffffffff
                        for block,blo,bhi in BLOCKS:
                            if blo<=addr<bhi: found[block].add((start,fsize,name)); break
                    if op in (32,33,34,35,40,41,42,43,46): regs[rt]=None
                # Unknown writes invalidate destination conservatively.
                elif op not in (18,16,19,): regs[rt]=None
    print("| Bloco | Endereço | Nome | Bytes |\n|---|---:|---|---:|")
    for block,_,_ in BLOCKS:
        for addr,size,name in sorted(found[block]): print(f"| {block} | 0x{addr:08X} | {name} | {size} |")
        if not found[block]: print(f"| {block} | — | — | — |")
if __name__=="__main__": main()
