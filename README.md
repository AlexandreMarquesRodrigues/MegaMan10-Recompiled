<p align="center">
  <img src="assets/MegaMan 10 Recomp BANNER 2.png" alt="Mega Man 10 Recompiled" width="1000">
</p>

# Mega Man 10 Recompiled

An experimental project to bring **Mega Man 10** (WiiWare, USA, title ID `WRXE`) to the PC as a **native application**, using **static recompilation** instead of emulation.

> 🚧 **Status: early development.** The game's PowerPC code is already being translated to C, but there is no runtime yet, so nothing is playable. See [Current progress](#current-progress).

## Goal

Build a native Windows (and later Linux) version of Mega Man 10 that runs the game's **original logic**, translated ahead of time from the Wii executable, on top of a modern host runtime. The approach follows the same idea as *N64: Recompiled*, *Unleashed Recompiled* and other recent Wii recompilation projects.

Planned features (none implemented yet):

- **Native executable**: no emulator, interpreter or JIT at runtime.
- **Controller support**: Xbox, PlayStation and generic gamepads through SDL3, plus keyboard. Inputs will be rebindable. The original game's Wii controller input will be mapped onto these devices.
- **Widescreen (16:9 and wider)**: the game renders for 4:3. Showing extra stage area needs game-specific patches (camera, scrolling, object culling) on top of the renderer, so this comes after the base port works.
- **Modern display options**: higher internal resolution, fullscreen/windowed modes, vsync.
- **Mod support**: hooks on game functions by their original addresses, asset replacement and scripting, so the community can change the experience like in other recompilations.
- **Preservation**: keep the game playable on current systems while respecting the original design.

## How it works

```
main.dol ──► function list + symbols ──► PowerPC → C translator (WiiRecomp)
                                                    │
                                                    ▼
          generated C  +  host runtime  ──►  native executable
                          ├─ memory map and CPU state
                          ├─ Wii OS / SDK replacements (HLE)
                          ├─ GX graphics → modern renderer
                          ├─ audio (AX)
                          ├─ input (SDL3)
                          └─ filesystem and saves
```

The generated C is a translation of the retail executable, so it is produced locally from your own copy and **never committed** to this repository.

### Why recompilation and not decompilation?

An earlier phase explored **matching decompilation** (decomp-toolkit + objdiff). It works, but each function needs many attempts to produce byte-identical output, and a finished decompilation would still need a PC runtime afterwards. Roughly 470 trivial functions were matched to 100% before the project switched to static recompilation, which reaches a playable port much sooner. The decompilation work is paused, not discarded: its symbol map is reused as input for the recompiler.

## Current progress

**Research and analysis**
- [x] Original game files obtained (own copy)
- [x] Ghidra environment configured
- [x] `main.dol` analyzed: 9,408 functions found, symbol map generated with decomp-toolkit
- [x] Matching-decompilation experiment (~470 trivial functions at 100%, build verified against the original executable hash), paused

**Static recompilation**
- [x] Switched to static recompilation (November 2026)
- [x] Function list for the recompiler generated from the symbol map (9,408 entries)
- [x] Translator run on `main.dol`: **9,404 of 9,408 functions fully translated**, 4 partial (to be investigated); 214 local switch tables lowered
- [ ] Build toolchain set up (MSYS2 MinGW-w64 GCC, CMake, Ninja, SDL) *(in progress)*
- [ ] Generated C compiles against a minimal runtime
- [ ] Boot: load the DOL and reach the game's entry point
- [ ] HLE of the Wii OS/SDK layer (threads, memory, filesystem, timing)
- [ ] Open a window and render the first frame (GX → host renderer)
- [ ] Input (Wii controller → keyboard and gamepads)
- [ ] Audio
- [ ] Game data loading and saves
- [ ] First playable stage
- [ ] Enhancements: widescreen, resolution scaling, mod hooks
- [ ] Public release

## Development log

### November 17, 2026
- Ran the WiiRecomp translator over `main.dol` with the function list generated from the symbol map: 9,404 functions fully translated, 4 partial, none left as stubs.
- Started setting up the build toolchain for compiling the generated C.

### November 16, 2026
- Evaluated the decompilation path with decomp-toolkit and objdiff, and matched ~470 trivial functions.
- Decided to pivot to static recompilation after reviewing existing Wii recompilation projects, in particular a WiiWare title ported with the same approach.
- Created the recompiler workspace and converted the symbol map into the recompiler's function list.

### October – November 2026
- Set up the decomp-toolkit project for `WRXE`, split configuration and analysis of the DOL (9,408 functions found).

### August 28, 2026
- Created the project repository.
- Started analyzing the Wii executable (`main.dol`) using Ghidra.
- Began investigating the game's PowerPC code and executable structure.
- Configured the development environment in **VS Code** using **CMake** and the **Visual Studio (MSVC)** compiler.
- Integrated the **SDL3** library via `CMakeLists.txt` for window management and input handling.
- Established the initial file structure (`main.cpp`, `input_manager.cpp`, `input_manager.h`) for controller mapping.

## Repository layout (planned)

```
games/megaman10/   per-game recompiler configuration (function list, HLE registry, quirks)
tools/             helper scripts (e.g. symbol map → function list)
runtime/           host runtime (planned)
private/           your own main.dol, never committed
```

## Building

The project is **not buildable yet**. Contributors can reproduce the translation step with their own `main.dol` (title `WRXE`) placed in `private/`, and [WiiRecomp](https://github.com/Eggscantfly/WiiRecomp) cloned next to this repository:

```
python ../WiiRecomp/recompile_wii.py private/main.dol --no-scaffold --config-dir games/megaman10 --out runtime/src/generated
```

## Legal

This is a fan-made research and preservation project, not affiliated with or endorsed by Capcom. *Mega Man* is a trademark of Capcom.

This repository does **not** contain the game, its assets, its executable or any code generated from it. You need your own legally obtained copy of the game.

## Credits and references

- [WiiRecomp](https://github.com/Eggscantfly/WiiRecomp): PowerPC → C translator used for the recompilation.
- [Robox Recompiled](https://github.com/Eggscantfly/robox-recompiled): a WiiWare recompilation used as the main reference for the runtime design.
- [decomp-toolkit](https://github.com/encounter/decomp-toolkit) and [objdiff](https://github.com/encounter/objdiff): analysis, splitting and the matching experiments.
- [Aurora](https://github.com/encounter/aurora): GameCube/Wii compatibility layer, being evaluated as a reference for the GX renderer.
- [Ghidra](https://ghidra-sre.org/) and [SDL3](https://libsdl.org/).

## License

To be decided. Code reused from GPL projects must keep a compatible license.
