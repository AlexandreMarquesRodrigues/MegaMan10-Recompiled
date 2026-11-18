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
LR, probable function and block totals. A repeated read of one address stops
after 100,000 consecutive reads, and the boot watchdog stops after 10 seconds.

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

Low memory is initialized with game ID `WRXE`, retail board type, MEM1/MEM2
sizes, memory arena bounds, and Wii bus/CPU clocks. In the observed startup,
`__start` also reads `0x800000f4` and `0x80000034` to check for optional
apploader callbacks; both remain zero for this direct DOL entry.
