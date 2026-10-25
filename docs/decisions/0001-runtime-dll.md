# 0001: Run-time DLL instead of editing the game files

- **Status:** accepted
- **Date:** 2026-10-25

## Context

OpenForts Classic changes both the code of `WF.exe` (timing, projection, controls, bug fixes) and the game's data (control, camera, AI and weapon values in `.xom` files). There are two ways to do this:

1. **Edit the files on disk:** binary-patch `WF.exe` and rewrite the `.xom` files.
2. **Change the game at run time:** load our own DLL into the game process, which changes code and data in memory.

## Decision

We use a run-time DLL, loaded through the import table of a patched copy of `WF.exe`. This is the same mechanism GOG already uses to load its `patch.dll`.

- The launcher creates the patched copy. The original `WF.exe` is never modified.
- The DLL (32-bit C++, MinHook) hooks or replaces game functions and applies byte patches in memory at start-up.
- Data changes are applied when the game loads its data. The exact technique is decided per change:
  - small value changes are written into the loaded XOM containers;
  - larger changes can go through a file overlay: the DLL redirects file reads to a folder of modified copies that the launcher generates locally from the player's own files.
- The launcher (C#, .NET, Velopack) handles installation detection, the hash check, settings and updates.

## Why not edit the files directly

A run-time DLL is not more limited than file editing. It runs inside the game process with the same rights as the game's own code, so it can do everything a file patch can do and more:

- **It can do anything a file patch can.** Byte patches are applied in memory at start-up, with the same effect as patching the file.
- **It can add large amounts of new code.** A file patch has to fit new code into unused gaps in the executable and write it in assembly. The DLL is ordinary C++ of any size: new menus, gamepad support, a crash handler, networking.
- **It can hook system interfaces.** OpenGL, DirectInput, DirectSound and file access can be intercepted. This is needed for vsync, the frame limiter, XInput and file overlays.
- **The original stays intact.** Uninstalling means deleting the patched copy. GOG Galaxy's integrity check and updates are not affected.
- **We never distribute Team17's files.** A patched `WF.exe` or modified `.xom` files would contain Team17's copyrighted content. Our releases contain only our own code and our own override data.
- **Settings work without regenerating files.** Most options (control profile, frame limit, fixes) can be switched in the launcher, and the DLL applies them at start-up.

## Limits and costs

- **Tied to one executable.** Addresses are specific to the GOG build, which is why the launcher checks the hash. A file patch has exactly the same limit.
- **The DLL must load before the game code runs.** The import table guarantees this, so no injector is needed.
- **Antivirus heuristics** sometimes flag hooking DLLs. Code signing and a clear, open-source project reduce this.

## Consequences

- The launcher is required to start the patched game. Starting the original `WF.exe` directly runs the unmodified game.
- Supporting another build (for example the retail CD version) means adding an address map for that build.
