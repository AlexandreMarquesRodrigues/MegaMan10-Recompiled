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

## First boot attempt

Run from the repository root so the executable can open `private/main.dol`:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
.\build\mm10.exe
```

The first run stops at the unsupported `HID2` SPR read in `PPCMfhid2`
(`0x80136c30`), called from `__OSPSInit` (`0x80138200`). This is the expected
first hardware interface requiring HLE; this milestone intentionally stops
there.
