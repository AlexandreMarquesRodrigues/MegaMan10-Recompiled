<p align="center">
  <img src="assets/MegaMan 10 Recomp BANNER 2.png" alt="Mega Man 10 Recompiled" width="1000">
</p>

# Mega Man 10 Recompiled

An experimental project focused on recompiling the Wii version of Mega Man 10 into a native PC application.

## Project Status

🚧 Early Development

The project is currently in the research and reverse-engineering stage.

### Current Progress

- [x] Original game files obtained
- [x] Ghidra environment configured
- [x] Analyze main.dol
- [x] Build toolchain set up (MSYS2 MinGW-w64 GCC, CMake, Ninja, SDL)
- [x] Generated C compiles against a minimal runtime (all 37 generated files, Release, 0 errors)
- [ ] Identify game initialization
- [ ] Identify rendering systems
- [ ] Identify input systems
- [ ] Identify audio systems
- [ ] Reconstruct game code
- [ ] Native PC executable
- [ ] Native PC release

## Development Log

### August 28, 2026

- Created the project repository.
- Started analyzing the Wii executable (`main.dol`) using Ghidra.
- Began investigating the game's PowerPC code and executable structure.
- Configured the development environment in **VS Code** using **CMake** and the **Visual Studio (MSVC)** compiler.
- Integrated the **SDL3** library into the project via automated configuration in `CMakeLists.txt` for native PC window management and input handling.
- Established the initial file structure (`main.cpp`, `input_manager.cpp`, and `input_manager.h`) for controller mapping.

### November 17, 2026

- The full translation (9,404 functions complete, 4 partial) now compiles in Release as a static library with a minimal runtime. The 4 partial functions contain an instruction the translator does not decode yet.
- Next: host runtime (guest memory, DOL loader, entry point) and the first boot attempt.

## Goals

The long-term goal is to create a native PC version of Mega Man 10 while preserving the original gameplay and improving compatibility with modern systems.

## Disclaimer

This is a fan-made research and preservation project.

No original game ROMs, copyrighted assets, or other proprietary game files are distributed by this repository.
