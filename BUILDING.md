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
3. First hardware I/O: write to `0xCC003004` in `__fill_mem` (`0x80004600`),
   called by `__OSInterruptInit` (`0x8013dea0`), LR `0x8013ded0`. The run stops
   here before emulating any MMIO device.

Low memory is initialized with game ID `WRXE`, retail board type, MEM1/MEM2
sizes, memory arena bounds, and Wii bus/CPU clocks. In the observed startup,
`__start` also reads `0x800000f4` and `0x80000034` to check for optional
apploader callbacks; both remain zero for this direct DOL entry.
