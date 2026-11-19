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

## Marco 11: null DVD dispatcher callback diagnosis

The reproduction command was `cmake --build build --parallel 4`, followed by
`.\build\mm10.exe 2>&1 | Tee-Object -FilePath build\marco11-run-01.log`.
Ninja completed successfully. The stop remains at the null indirect call at
`0x80149834`, with LR `0x80149838`, in `fn_801492A0`. `func_list.json` and the
DOL identify the function address, but provide no confirmed SDK symbol name.

The runtime now keeps a rolling log of the last 20 MMIO operations. The
reproduction's complete list and the last 16 distinct functions are in
`build/marco11-run-01.log`. The last MMIO operation is `CC003024` read as zero
at LR `0x801373E8`, attributed by the trace to `OSGetResetCode`; there are no
DI (`CC006000`) or DVD MMIO accesses before this stop. The final 16 distinct
functions are, oldest first: `OSInitThreadQueue`, `DVDLowUnmaskStatusInterrupts`,
`DVDLowMaskCoverInterrupt`, `memset`, `__fill_mem`, `memcpy`,
`DVDSetAutoFatalMessaging`, `DCInvalidateRange`, `DVDInquiryAsync`,
`fn_8014AE40`, `fn_80148F80`, `fn_8014AF50`, `fn_8014AEB0`,
`OSDisableInterrupts`, `OSRestoreInterrupts`, `fn_801492A0`.

At the stop, `r13=0x80680C40`; the load at `0x80149820` is
`lwz r12,-0x5458(r13)`, so the callback slot is `r13-0x5458 = 0x8067B7E8`.
Marco 12 corrects the earlier section identification: this address is inside
data6, not BSS. The effective slot value at the stop is `0x00000000`. The DOL
sequence at the end of the dispatcher is:

```text
80149820  lwz   r12,-0x5458(r13)
80149824  lis   r4,-0x7feb
80149828  mr    r3,r29
8014982c  addi  r4,r4,-0x67b0
80149830  mtctr r12
80149834  bctrl
```

The full DOL disassembly from function entry through `bctrl` is saved in
`build/marco11-fn_801492A0.disasm.txt`; the extracted bytes are in
`build/marco11-fn_801492A0.bin`. The translated version is
`runtime/src/generated/funcs_0016_80143d70.c`.
`fn_801492A0` reads a request command at `+8`, dispatches known command cases,
and reaches the global hook for the inquiry command. The trace shows this is
entered through the normal `DVDInquiryAsync` request path, with interrupts
disabled/restored locally while the request is queued; it is not an IOS IPC
interrupt handler or a DVD completion interrupt handler. The request's
completion/dispatch still depends on the DVD device path.

Marco 12 found an SDK store and supersedes the earlier “no stores/BSS”
conclusion. See the Marco 12 section for exact source bytes, runtime snapshots,
the `ClearMEM2Arena`/`DCZeroRange` writer, and the static scan. The proposed
interrupt-safe IOS/IPC and device response design remains in
[docs/ios-ipc-design.md](docs/ios-ipc-design.md).

Final Marco 11 stop: target `0x00000000`, call site `0x80149834`,
`fn_801492A0` (SDK name unconfirmed), LR `0x80149838`. No later iteration was
run because at that time it was classified as the (b) device boundary. Marco
12 corrected that classification after finding the preceding `ClearMEM2Arena`
write.

## Marco 12: prove the callback slot writer

The slot is `r13-0x5458 = 0x8067B7E8`. It lies in DOL data6
`0x80678C40..0x8067C9E0`, at section offset `0x2BA8` and DOL file offset
`0x350E08`. The 16 file bytes at `0x350E00..0x350E0F` are
`80 32 1D 18 00 00 00 01 80 14 62 90 64 76 64 2E`; the initialized slot word
is `0x80146290`. That value resolves to `fn_80146290`, a one-instruction
`blr` stub in the DOL.

With `MM10_WATCH=ON`, the three 16-byte snapshots (`addr=0x8067B7E0`) were:

| Phase | Bytes | Slot |
| --- | --- | --- |
| Immediately after DOL load | `80 32 1D 18 00 00 00 01 80 14 62 90 64 76 64 2E` | `0x80146290` |
| Entry to `DVDInit` (`0x801462A0`) | `00` × 16 | `0x00000000` |
| Null-call stop | `00` × 16 | `0x00000000` |

The watchpoint logged all overlapping writes. The BSS clear initially zeroes
the broad BSS span, then the data6 copy restores `0x80146290`. After boot
starts, one translated guest write touches the slot:

```text
MM10_WATCH_WRITE va=8067b7e8 size=4 overlap=8067b7e8 bytes=00000000
  LR=80137428 function=DCZeroRange@801394d0 r3=8067b7ff r4=04a00001
```

`0x80137428` is the return address after the call at `0x80137424` in
`ClearMEM2Arena` (`0x801373D0`). Its code reads `OSGetMEM2ArenaHi` and
`OSGetMEM2ArenaLo`, computes the range, then calls `DCZeroRange` at
`0x801394D0`; that helper executes `dcbz` at `0x801394EC`. The cache line
`0x8067B7E0..0x8067B7FF` therefore zeroes the initialized data6 callback slot.
The `DCZeroRange` event occurred before the `DVDInit` entry snapshot.

The static raw-DOL scan is `tools/find_writes.py`; its full output is
`build/marco12-static.txt`. It finds no immediate effective-address store
formed from the observed `r13`, `lis/addis` plus displacement, or a loaded
`0x80680000` base that resolves exactly to the slot. It identifies the
parameter-based `dcbz` sites; the relevant one is
`0x801394EC DCZeroRange`, called by `ClearMEM2Arena` at `0x80137424`. The other
unresolved `dcbz` sites are in `fn_80294404` (`0x80294528`, `0x8029458C`,
`0x80294630`, `0x802946D4`) and `fn_802947C4` (`0x80294844`, `0x80294858`);
the runtime watchpoint shows they did not touch this slot in this run.

Direct call xrefs in the DOL: `fn_801492A0` is called by
`fn_80147F60` (`0x8014808C`), `fn_80148440` (`0x801484C4`),
`fn_80148510` (`0x8014869C`), `fn_80148920` (`0x80148A7C`),
`fn_80148B60` (`0x80148C14`), `fn_80148F80` (`0x8014927C`), and twice by
`fn_80149850` (`0x80149EE8`, `0x80149F8C`). `DVDInquiryAsync` is called by
`OSInit` (`0x80137D5C`). `OSInit` also calls `DVDInit` (`0x80137D0C`) and
`IPCCltInit` (`0x80137B90`). The complete 105-callsite list of callers outside
the DVD/IPC/IOS/NAND/ES SDK module ranges is in `build/marco12-static.txt`.
It includes `SCInit`/`SCReloadConfFileAsync` paths into NAND, OS/system paths
into ES, plus title functions calling IOS services; this DOL uses DVD as well
as NAND/ES/IOS, not only NAND/ES.

Marco 13 supersedes this classification: the writer ran, and its translated
`DCZeroRange` was given a corrupted MEM2 arena start because the runtime's
low-memory MEM2 fields were missing/misplaced. The reset path was not evidence
that clearing loaded data6 was intentional.

Commands that worked:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMM10_WATCH=ON
cmake --build build --parallel 4
.\build\mm10.exe 2>&1 | Tee-Object -FilePath build\marco12-watch-run-02.log
python tools/find_writes.py 'C:\Projetos\MegaMan10-Recomp\decomp\dtk-template-main\orig\WRXE\main.dol' games\megaman10\func_list.json --output build\marco12-static.txt
```

## Marco 13: MEM2 arena bounds and the DVD callback

### DOL disassembly and clear-range calculation

Disassembled the original DOL (the source path in `AGENTS.md`) at
`0x801373D0` for 484 bytes and at `0x801394D0` for 44 bytes. Both ranges map
through text1 (`file offset 0x27c0`, VA `0x8000d640`). The working extraction
and disassembly commands are:

```powershell
@'
import struct
p=r'C:\Projetos\MegaMan10-Recomp\decomp\dtk-template-main\orig\WRXE\main.dol'
b=open(p,'rb').read()
for name,start,size in [('clearmem2',0x801373d0,484),('dczero',0x801394d0,44)]:
    off=0x27c0+(start-0x8000d640)
    open(f'build/{name}.bin','wb').write(b[off:off+size])
'@ | python -
& .\build\powerpc-eabi-objdump.exe -D -EB -b binary -m powerpc:common --adjust-vma=0x801373d0 .\build\clearmem2.bin
& .\build\powerpc-eabi-objdump.exe -D -EB -b binary -m powerpc:common --adjust-vma=0x801394d0 .\build\dczero.bin
```

`ClearMEM2Arena` first checks `OSGetResetCode` at `0x8013FE70`. On its normal
path it gets MEM2 arena high from `OSGetMEM2ArenaHi` (`lwz r3,-0x3fac(r13)` at
`0x80138e40`), gets low from `OSGetMEM2ArenaLo` (`lwz r3,-0x54d4(r13)` at
`0x80138e70`), subtracts low from high for the byte count, and gets low again
for the starting address. It then calls `DCZeroRange` at `0x80137424`. The
alternate reset/region-descriptor branches use descriptor words at
`0x805c820c` and `0x805c8210` to choose a subrange; their `DCZeroRange` calls
are at `0x80137490`, `0x801374f8`, `0x8013753c`, and `0x8013758c`.

`DCZeroRange` returns for a zero size. Otherwise it computes
`ceil((size + (start & 31))/32)` cache lines, places that count in CTR, then
loops over `dcbz r0,r3` and advances r3 by 32 bytes. `dcbz` clears the whole
32-byte line containing r3, so the first address is `start & ~31` and the
exclusive end is that address plus 32 times the line count. The generated
implementation aligns with `& ~0x1f` and emits eight `MEM_W32` zero stores,
which map to guest RAM through the same address translator as ordinary stores.

### Cause and correction

Before the fix, the boot snapshot had MEM2 simulated size at `0x8000311c`,
but it had put the MEM2 end at `0x80003128`, left the actual arena-begin word
`0x80003124` zero, and left the physical-size/end words `0x80003118` and
`0x80003120` zero. The SDK therefore initialized
`r13-0x54d4` (`0x8067b76c` with live `r13=0x80680c40`) to `0xffffffff` and
`r13-0x3fac` (`0x8067cc94`) to `0x94000000`. At `DCZeroRange` entry it received
`r3=0xffffffff`, `r4=0x94000001`: 77,594,625 lines, covering the wrapped
interval `[0xffffffe0,0x194000000)`. That interval wraps through MEM1 and
cleared the initialized DVD callback at `0x8067b7e8`.

The runtime now lays out Wii IOS low-memory fields at their actual addresses:

| Address | Runtime value | Meaning |
| --- | ---: | --- |
| `0x80003100` | `0x01800000` | MEM1 physical size |
| `0x80003104` | `0x01800000` | MEM1 simulated size |
| `0x80003108` | `0x81800000` | MEM1 end |
| `0x8000310c` | `0x00000000` | MEM1 arena begin |
| `0x80003110` | `0x81800000` | MEM1 arena end |
| `0x80003118` | `0x04000000` | MEM2 physical size |
| `0x8000311c` | `0x04000000` | MEM2 simulated size |
| `0x80003120` | `0x94000000` | MEM2 end |
| `0x80003124` | `0x90000800` | MEM2 arena begin |
| `0x80003128` | `0x94000000` | MEM2 arena end |

The direct-DOL runtime has no emulated IOS reservation or IPC buffer, so its
MEM2 arena end equals the 64 MiB physical end. Dolphin's IOS setup computes
these bounds from IOS-specific reserved and IPC-buffer sizes; it sets arena
begin to `MEM2_BASE+0x800`. Dolphin's `Boot_BS2Emu.cpp` writes legacy fields
`0x80000028` (physical MEM1 size), `0x80000030` (zero), `0x80000034`
(`0x817fec60`), `0x800000f0` (simulated MEM1 size), `0x800000f4`
(`0x8179b500`), and the same bus/CPU clocks used here. Our direct-DOL path
keeps `0x80000030` at the aligned end of loaded DOL/BSS (`0x8067e9a0`) so
MEM1 arena clearing preserves the image; callback words `0x34` and `0xf4`
remain zero because we do not run an apploader. Those are deliberate direct
boot differences. The corrected runtime call now enters `DCZeroRange` with
`r3=0x90000800`, `r4=0x03fff800`, 2,097,088 lines, and clears exactly
`[0x90000800,0x94000000)`.

At the corrected call, `r13=0x80680c40`; `r13-0x54d8=0x8067b768` contains
MEM1 arena low `0x8067e9a0`; `r13-0x54d4=0x8067b76c` contains MEM2 arena low
`0x90000800`; `r13-0x3fb0=0x8067cc90` contains MEM1 arena high `0x81800000`;
and `r13-0x3fac=0x8067cc94` contains MEM2 arena high `0x94000000`. The
neighboring words are recorded in `build/marco13-run-04.log`.

`load_dol`'s `max_mem1` is correct for this source DOL: the BSS ends at
`0x8067e994`, after data sections, and the 32-byte rounded direct-boot arena
start is `0x8067e9a0`. The loader clears BSS before copying sections. `dcbz`
alignment is 32 bytes. `ppc_host_ptr` maps cached MEM1 `0x80000000–0x81800000`
to `g_mem1`, cached MEM2 `0x90000000–0x94000000` to the separate 64 MiB
`g_mem2`, and their uncached aliases to those same respective buffers; no
MEM2 address aliases into MEM1.

### Verification and stop

With `MM10_WATCH=ON`, the watch log records both the entry registers and
aligned interval for `DCZeroRange`. The DVD slot remains `0x80146290` at DOL
load, at `DVDInit` entry (`0x801462a0`), and at the next stop. There is no
watch write to the slot after section loading. For this run, MM10_WATCH allows
pre-DVD VI accesses to proceed so the slot check completes, then stops at the
first VI access after `DVDInit`.

Commands that worked (Ninja completes successfully; the executable's
nonzero status is its intentional hardware-boundary stop):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMM10_WATCH=ON
cmake --build build --parallel 4
.\build\mm10.exe 2>&1 | Tee-Object -FilePath build\marco13-run-04.log
```

Iteration record (four runs, within the ten-run limit):

| Run | Observation / stop |
| --- | --- |
| `marco13-run-01.log` | Before the low-memory fix: `DCZeroRange` entered with `r3=ffffffff`, `r4=94000001`; data6 slot was cleared and execution stopped at the null DVD callback, `fn_801492A0`, LR `0x80149838`. |
| `marco13-run-02.log` | After the low-memory fix: MEM2 clear was confined to `0x90000800..0x94000000`, DVD slot survived, then the old 10-second watchdog stopped at `fn_801A1A30`, LR `0x801A321C`. |
| `marco13-run-03.log` | First attempt to stop on VI MMIO halted too early, before `DVDInit`, at `0xCC00206C`; the stop was gated on DVDInit for the final run. |
| `marco13-run-04.log` | Final run: slot remained `0x80146290` at DVDInit and stop; halted at the first post-DVD VI access, `0xCC00206E`. |

Final stop: VI halfword read at `0xCC00206E`, issued by DOL instruction
`lhz r0,0x206e(r3)` at `0x8013CD80` in `fn_8013CD60` (the DOL symbol name is
generic; no SDK name is confirmed). Its LR is `0x80019F2C`. This is the first
post-`DVDInit` VI access; the watch snapshot at the stop still shows
`0x8067B7E8=0x80146290`. The entry trace can show a stale nested
`OSRestoreInterrupts` label, so the function and instruction above are
identified from the LR, effective address, and DOL disassembly rather than
that trace label.
## Marco 15: VI reader, MMIO watch, and OSReport

Commands that worked:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMM10_WATCH=OFF
cmake --build build
.\build\mm10.exe 2>&1 | Tee-Object -FilePath build\marco15-run-02.log
```

The MMIO watch now logs each address once, with an optional stop through
`MM10_STOP_ON_MMIO=VI` or `GX`. With the option off, the run passed the VI
read and reached an SDK wait in `fn_801A31D0`; see `build/marco15-run-03.log`.
The wait repeatedly tests `0x8067BB60` (SDA `r13-0x50E0`), which stayed `1`.
OSReport text is emitted by the `__FileWrite` HLE; the run reported
`Not enough IPC arena`, failed DVD command allocation, and `USB ERR` before
the wait. The callback path that clears the flag is `fn_801A2A60`.
