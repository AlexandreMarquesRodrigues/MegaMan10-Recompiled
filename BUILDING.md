# Build and translation commands

The generated translation is compiled in Release as a static library and
linked into `build/mm10.exe`. The current milestone runs from `private/main.dol`
until the first unsupported guest operation, which is logged with a guest PC,
function and register snapshot.

## Tools checked

```powershell
gcc --version
ninja --version
cmake --version
Test-Path 'C:\msys64\mingw64\include\SDL2'
```

## Regenerate the translation

The translator writes a module manifest beside its input DOL, so use a
temporary input copied from the source DOL named in `AGENTS.md`:

```powershell
New-Item -ItemType Directory -Force .translation-input | Out-Null
Copy-Item -LiteralPath 'C:\Projetos\MegaMan10-Recomp\decomp\dtk-template-main\orig\WRXE\main.dol' -Destination .translation-input\main.dol -Force
python ..\WiiRecomp\recompile_wii.py .translation-input\main.dol --no-scaffold --config-dir games\megaman10 --out runtime\src\generated
```

## Compile generated C

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

## Marco 5: first hardware I/O

Run from the repository root so the executable can open `private/main.dol`:

Successful commands for this milestone:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
.\build\mm10.exe
```

The executable exits nonzero at the deliberate guest stop. Ordered stops and
the change made before the next run:

1. `HID2` read at `0x80136c30`, `PPCMfhid2`, LR `0x80138210` in `__OSPSInit`.
   Added host SPR state; zero HID2 reset value lets SDK initialization proceed.
2. Indirect call to `0x8015fd0c` in `OSExceptionInit`. This is the interior
   entry `__save_gpr+0x18`, used to save r20-r31. Added suffix handling for the
   CodeWarrior save/restore GPR and FPR thunks.
3. First hardware I/O: the initial observation was a write to `0xCC003004`
   reported under `__fill_mem` (`0x80004600`), LR `0x8013ded0`, called by
   `__OSInterruptInit` (`0x8013dea0`). The DOL disassembly in Marco 6 shows
   the actual store at `0x8013def0` inside `__OSInterruptInit`; the trace name
   was stale because `ppc_trace` updates only at generated function entry.

## Marco 6: SDK boot MMIO map (shadow only)

The runtime keeps byte-addressed shadows for `0xCC000000–0xCC00FFFF` and
`0xCD000000–0xCD8FFFFF`. Reads return the last bytes written (zero initially);
there is no device behavior. It reports per-address access counts, first value,
first and last LR/function contexts, and block totals. A repeated read of one
address stops after 100,000 consecutive reads; the boot watchdog stops after
10 seconds.

Disassemble a DOL text range by mapping its VA through the DOL text-section
header, extracting those bytes, then forcing big-endian PPC mode:

```powershell
$dol = 'C:\Projetos\MegaMan10-Recomp\decomp\dtk-template-main\orig\WRXE\main.dol'
$objdump = '.\build\powerpc-eabi-objdump.exe'
# The selected section has file offset 0x27c0 and VA 0x8000d640.
$start = 0x8013dea0; $end = 0x8013df10
$source = [IO.File]::ReadAllBytes($dol)
$offset = 0x27c0 + ($start - 0x8000d640)
[IO.File]::WriteAllBytes('.\build\osinterrupt-range.bin', $source[$offset..($offset + $end - $start - 1)])
& $objdump -D -EB -b binary -m powerpc:common --adjust-vma=0x8013dea0 .\build\osinterrupt-range.bin
```

Working build and boot commands:

```powershell
cmake --build build --parallel 4
.\build\mm10.exe
```

This reaches the EXI status poll at `0xCD00680C` in `EXISync`; the shadow reads
zero, so the repeated-read watchdog stops execution before any device is
emulated. Rebuild after runtime edits before running the executable.

## SDK-name auto-identification

`recompile_wii.py` reads `identified_funcs.json` from `--config-dir` and merges
SDK names that `hle_name_map.py` can resolve. The Mega Man 10 config has no such
file, so the translator reports `+0 auto from Dolphin DB`. There is no
recompiler CLI option that runs the matching step. In the checked-out
WiiRecomp clone, the referenced `dolphin_db.py` producer/database is absent;
`sig_scan.py` only recognizes a few narrow patterns (such as memset/memcpy),
not the full SDK name set. Automatic SDK identification would require that
signature database/matcher to produce `games/megaman10/identified_funcs.json`
(or equivalent data supplied in the configured directory), with entries
mapping guest addresses to confirmed SDK function names.

## Marco 7: first name-based HLE (EXI)

The configured registry maps 11 EXI-family addresses to synchronous HLE
handlers. `__EXIProbe` returns false and `EXIGetID` returns false without
changing its output buffer, matching the callers' no-device path. Transfers,
selection and locking complete immediately with success. The inventory script
counts named functions by SDK prefix and can mark names seen in a boot log.

Working commands for this milestone:

```powershell
New-Item -ItemType Directory -Force .translation-input | Out-Null
Copy-Item -LiteralPath 'C:\Projetos\MegaMan10-Recomp\decomp\dtk-template-main\orig\WRXE\main.dol' -Destination .translation-input\main.dol -Force
python ..\WiiRecomp\recompile_wii.py .translation-input\main.dol --no-scaffold --config-dir games\megaman10 --out runtime\src\generated
cmake --build build --parallel 4
.\build\mm10.exe 2>&1 | Tee-Object -FilePath build\marco7-run-01.log
python tools\sdk_inventory.py games\megaman10\func_list.json --boot-log build\marco7-run-01.log
```

Historical Marco 7 result (superseded for the thread stop by Marco 9):
translation produced 9,408 functions, 11 HLE, 4 PARTIAL, 0 STUB; Release
compilation completed. The then-observed boot stop was a null indirect call
at `0x00000000`, LR
`0x80140AB4`, in `__OSThreadInit` (`0x80140A00`); trace's current label was
`OSSetCurrentContext` and may be stale. The DOL shows the null target loaded
from an SDA callback slot and called at `0x80140AB0`, while thread context is
being initialized. This is the requested context/thread boundary, so no
context-switch emulation was added.

Low memory is initialized with game ID `WRXE`, retail board type, MEM1/MEM2
sizes, memory arena bounds, and Wii bus/CPU clocks. In the observed startup,
`__start` also reads `0x800000f4` and `0x80000034` to check for optional
apploader callbacks; both remain zero for this direct DOL entry.

## Marco 9: DOL BSS/data overlap and the `__OSThreadInit` callback

Correction to the preliminary Marco 8 diagnosis: `0x803daa28` is the
effective address only while `r13=0x803dfed0`. `OSSetCurrentContext` restores
`r13=0x80680c40` before the failing call, so the actual slot at
`0x80140aa4` is `0x8067b798`. That address is inside DOL `data6`; its original
word is `0x801409f0`, whose instruction is `blr`. The BSS clear erased this
SDK default callback, which is why `bctrl` saw zero. The failure was our DOL
loader order, not evidence that a scheduler/HLE callback had to be invented.
The fix clears BSS before copying DOL sections. The callback now survives and
the boot passes `0x80140ab0` without an experiment.

`load_dol` order before the fix: copy all 18 section slots, then clear the
header BSS range. Fixed order: clear BSS first, then copy all section slots.
Audit mode `MM10_DOL_AUDIT=1` reports the requested slot and CRC-32 of each
nonempty data section. The original-order run logged `803daa28=0` before and
after BSS clear; all data sections matched the file immediately after copy,
but the clear corrupted `data6` and `data7`. The corrected run still logs
`803daa28=0`; after section copy it reports `8067b798=801409f0` and every
data-section CRC matches the file. In the pre-fix crash dump, `r13=80680c40`
and `CTR=0`, confirming the actual effective slot `8067b798` was zero. The
DOL bytes at callback target `801409f0` are `4e800020` (`blr`).

### DOL layout from the original file

Ranges use an exclusive end address. `—` means the header slot is empty.

| Section | Address | Size | File offset | BSS relation |
| --- | ---: | ---: | ---: | --- |
| text0 | `80004000` | `000026c0` | `00000100` | outside |
| text1 | `8000d640` | `002c29e0` | `000027c0` | outside |
| text2–text6 | — | — | — | empty |
| data0 | `800066c0` | `00005ca0` | `002c51a0` | outside |
| data1 | `8000c360` | `000012e0` | `002cae40` | outside |
| data2 | `802d0020` | `00000060` | `002cc120` | outside |
| data3 | `802d0080` | `00000020` | `002cc180` | outside |
| data4 | `802d00c0` | `00010840` | `002cc1a0` | outside |
| data5 | `802e0900` | `00071880` | `002dc9e0` | ends at BSS start |
| data6 | `80678c40` | `00003da0` | `0034e260` | wholly inside BSS |
| data7 | `8067d7a0` | `000011e0` | `00352000` | wholly inside BSS |
| data8–data10 | — | — | — | empty |

Header BSS range: `[80352180, 8067e994)`, size `0032c814`. No data section
covers `803daa28`; `data6` and `data7` are the sections erased by the old
load order.

### RAM CRC-32 comparison

The file CRC is also the RAM CRC after the corrected copy. The old loader's
first phase was after copies but before its BSS clear; its second phase was
after that clear. Logs are `build/marco9-before.log` and
`build/marco9-after-fix.log`.

| Section | File / old pre-clear | Old post-clear RAM | Corrected post-copy RAM |
| --- | ---: | ---: | ---: |
| data0 | `1e09ff0e` match | `1e09ff0e` match | `1e09ff0e` match |
| data1 | `e25a7469` match | `e25a7469` match | `e25a7469` match |
| data2 | `0491db5e` match | `0491db5e` match | `0491db5e` match |
| data3 | `4d756c7a` match | `4d756c7a` match | `4d756c7a` match |
| data4 | `13c93e51` match | `13c93e51` match | `13c93e51` match |
| data5 | `81c543ab` match | `81c543ab` match | `81c543ab` match |
| data6 | `8140e2e2` match | `590bd771` mismatch | `8140e2e2` match |
| data7 | `ea6dbf9e` match | `f650c46a` mismatch | `ea6dbf9e` match |

### Raw instruction scan

Scanned every instruction in each nonempty DOL text section, independently
of `func_list.json`. The only accesses to displacement `-0x54a8(r13)` are
three reads; there are no stores:

```text
80140aa4  lwz r12,-0x54a8(r13)
8014112c  lwz r12,-0x54a8(r13)
801411fc  lwz r12,-0x54a8(r13)
```

No adjacent `lis`/`addi` or `lis`/`ori` pair in the raw text forms
`0x803daa28`.
That address comes from the runtime's initial `r13` plus the signed offset;
the context restore changes `r13`, making the live slot `0x8067b798`.

Commands that reproduced the audit and build:

```powershell
@'
import struct
p=r'C:\Projetos\MegaMan10-Recomp\decomp\dtk-template-main\orig\WRXE\main.dol'
b=open(p,'rb').read()
for i in range(7):
    off=struct.unpack_from('>I',b,i*4)[0]
    addr=struct.unpack_from('>I',b,0x48+i*4)[0]
    size=struct.unpack_from('>I',b,0x90+i*4)[0]
    if addr <= 0x80140a00 < addr+size:
        start=off+0x80140a00-addr
        for va in range(0x80140a00,0x80140ab4,4):
            pos=start+va-0x80140a00
            print(f'{va:08x}: {b[pos:pos+4].hex()}')
print('BSS', *(hex(struct.unpack_from('>I',b,o)[0]) for o in (0xd8,0xdc)))
'@ | python -
@'
import struct
b=open(r'C:\Projetos\MegaMan10-Recomp\decomp\dtk-template-main\orig\WRXE\main.dol','rb').read()
memops={32:'lwz',33:'lwzu',34:'lbz',35:'lbzu',36:'stw',37:'stwu',38:'stb',39:'stbu',40:'lhz',41:'lhzu',42:'lha',43:'lhau',44:'sth',45:'sthu',46:'lmw',47:'stmw'}
for s in range(7):
    off=struct.unpack_from('>I',b,s*4)[0]; va=struct.unpack_from('>I',b,0x48+s*4)[0]; size=struct.unpack_from('>I',b,0x90+s*4)[0]
    for p in range(off,off+size,4):
        x=struct.unpack_from('>I',b,p)[0]; op=x>>26; ra=(x>>16)&31; imm=x&0xffff; imm=imm-0x10000 if imm&0x8000 else imm
        if op in memops and ra==13 and imm==-0x54a8: print(f'{va+p-off:08x}: {memops[op]} r{(x>>21)&31},-0x54a8(r13)')
        if op==15 and ra==0 and p+4<off+size:
            y=struct.unpack_from('>I',b,p+4)[0]; rt=(x>>21)&31; op2=y>>26; rt2=(y>>21)&31; ra2=(y>>16)&31; lo=y&0xffff; hi=(x&0xffff)<<16
            if rt==rt2 and ra2==rt and op2==14 and ((hi+(lo-0x10000 if lo&0x8000 else lo))&0xffffffff)==0x803daa28: print(f'{va+p-off:08x}: lis/addi -> 803daa28')
            if rt==rt2 and ra2==rt and op2==24 and (hi|lo)==0x803daa28: print(f'{va+p-off:08x}: lis/ori -> 803daa28')
'@ | python -
cmake --build build --parallel 4
$env:MM10_DOL_AUDIT = '1'
.\build\mm10.exe 2>&1 | Tee-Object -FilePath build\marco9-after-fix.log
Remove-Item Env:\MM10_DOL_AUDIT
```

The pre-fix run stopped at the null indirect call in `__OSThreadInit`,
`bctrl` at `0x80140ab0`, LR `0x80140ab4`. After the loader fix, startup
passed that call and stopped at DSP MMIO `0xcc00500a` after the runtime's
100,000-read hardware wait guard. The trace names `DCFlushRange`
(`0x80139440`), LR `0x801391a8`, called by `__OSInitAudioSystem`
(`0x80139150`). This is the audio/DSP hardware boundary; no thread HLE or
NOP-callback experiment was added. `docs/os-threads-design.md` remains a
future scheduler proposal; its earlier claim that this stop itself required
context switching is superseded by this finding.

## Marco 10: boot audio HLE and next hardware stop

`__OSInitAudioSystem` is at `0x80139150` in the source DOL (464 bytes).
The stop label `DCFlushRange` was stale: that helper returned at
`0x801391a8`, but the actual unsupported MMIO read loop is at `0x801391c8`.
It reads DSP register `0xCC00500A` and spins while bit 0 is set. The SDK
writes `0x08AC`, then sets bit 0 to start a DSP command; it waits for that
busy/start bit to clear before proceeding. Later in the function it polls DSP
status registers, programs DSP DMA registers, and starts another command.

Outside DSP MMIO, the function conditionally calls `__AIClockInit(1)` when
the SDA flag at `r13-0x4008` is clear (including its IOS IPC bookkeeping and
tick reads), calls `OSGetArenaHi`, copies 0x80 bytes from `0x81000000` to
`arena_hi-0x80`, copies 0x80 bytes from `0x80314250` to `0x81000000`, calls
`DCFlushRange(0x81000000, 0x80)`, waits 0x892 time-base ticks, then copies
0x80 bytes from `arena_hi-0x80` back to `0x81000000`. There are no direct
low-memory or OS queue/heap/interrupt writes and no `ICInvalidate` call in
this function. ARAM/DSP transfer setup is through DSP MMIO, not direct RAM
writes. The HLE retains the non-DSP calls, copies, and delay, and omits the
DSP register commands/status polling because the host runtime does not model
the DSP/ARAM engine.

The HLE is registered at `0x80139150` in `games/megaman10/hle_registry.json`
and implemented by `runtime/hle_audio.c`. Regeneration and build commands:

```powershell
New-Item -ItemType Directory -Force .translation-input | Out-Null
Copy-Item -LiteralPath 'C:\Projetos\MegaMan10-Recomp\decomp\dtk-template-main\orig\WRXE\main.dol' -Destination .translation-input\main.dol -Force
python ..\WiiRecomp\recompile_wii.py .translation-input\main.dol --no-scaffold --config-dir games\megaman10 --out runtime\src\generated
cmake --build build --parallel 4
.\build\mm10.exe 2>&1 | Tee-Object -FilePath build\marco10-run-01.log
```

| Stop | Function | SDK expectation / observed state | Decision |
| --- | --- | --- | --- |
| `0xCC00500A`, loop at `0x801391C8` (previous stop; LR `0x801391A8`) | `__OSInitAudioSystem` (`0x80139150`, DOL name confirmed) | DSP command busy/start bit 0 must clear; subsequent DSP status and DMA handshakes complete audio-system startup. | Added `hle_OSInitAudioSystem`; kept AI clock/IPC, buffer copies, `DCFlushRange`, and time-base delay; omitted DSP MMIO only. Run passed this stop. |
| `bctrl` at `0x80149834`, LR `0x80149838`, target/CTR `0` | `fn_801492A0` (generic generated label; no SDK name confirmed). The preceding named call chain includes `DVDInquiryAsync` at `0x8014A320`. | Dispatcher loads callback from `-0x5458(r13)`; the current slot is zero. This is DVD asynchronous request/callback handling, whose request and callback state is observable. | Stopped at the DVD boundary; no HLE. |

Final stop: indirect call target `0x00000000`, call site `0x80149834`,
function `fn_801492A0` (generic label, not a confirmed SDK name), LR
`0x80149838`. DOL disassembly confirms the `bctrl` at that address and the
preceding callback load; the label is not misidentified as a named DVD SDK
routine. The runtime trace stopped after one iteration at this stateful DVD
callback path, within the requested 12-iteration limit.