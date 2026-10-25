# OpenForts Classic

Improvements for the original *Worms Forts: Under Siege* on Windows: correct game speed at any frame rate, widescreen, modern controls, bug fixes and a launcher with automatic updates.

**Status:** in development, no release yet. See the [roadmap](ROADMAP.md).

## Requirements

- Your own copy of *Worms Forts: Under Siege*. Only the GOG version is supported for now.

## Using it

Put `openforts-classic-launcher.exe` into your Worms Forts folder (the folder that contains `WF.exe`) and start it from there. It creates `WF-OpenForts.exe` and `OFCRT.dll` next to the game and leaves your original files untouched.

## Building

Requirements: Visual Studio Build Tools 2022 with the C++ workload, CMake, Rust (stable) and Node.js with pnpm.

```
cmake -S runtime -B runtime/build -A Win32
cmake --build runtime/build --config Release

cd launcher
pnpm install
pnpm tauri build
```

The runtime has to be built first, because the launcher embeds `OFCRT.dll`. For development, `pnpm tauri dev` runs the launcher from its build folder; set `OPENFORTS_GAME_DIR` to your game folder first.

## License

[GPL-3.0](LICENSE).

## Disclaimer

OpenForts Classic is an unofficial fan project. It is not affiliated with, endorsed by or connected to Team17. *Worms* and *Worms Forts: Under Siege* are trademarks of Team17 Digital Limited.

This repository contains no game files. OpenForts Classic works only with a copy of the game you own, and it doesn't modify your original installation.
