# Build and translation commands

The generated translation is compiled in Release as a static library for
compile validation only. It is not linked into or executed as a game.

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
