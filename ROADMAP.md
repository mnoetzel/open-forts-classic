# OpenForts Classic: Roadmap

OpenForts Classic improves the original *Worms Forts: Under Siege* (Team17, 2004) on Windows. It patches the player's own installation at run time and ships no Team17 files. It is built before the full rewrite ([OpenForts](https://github.com/mnoetzel/openforts)), partly to find out what the rewrite should feel like.

How it works technically is described in [docs/decisions/0001-runtime-dll.md](docs/decisions/0001-runtime-dll.md).

## Supported versions

- **GOG release only** for now. The launcher checks the hash of `WF.exe` and refuses unknown builds.
- Other builds (retail CD, other regions) can be added later. Each one needs its own address map.

## Principles

- The original installation stays untouched. Everything we change is applied to a separate patched copy or at run time.
- No new art. Classic works with the original assets.
- Every gameplay change is optional where that is practical, so the original experience stays available.
- LAN play requires both players to run the same Classic version with the same gameplay settings, because the game uses lockstep networking.

## 0.1: Playable on modern hardware

### Frame rate

- Fix the frame-time truncation in the main loop. The fractional milliseconds are carried over instead of dropped, so game speed is correct at every frame rate. Today the game runs slower at high frame rates and freezes at 1000 fps or more.
- Add a frame limiter (default: the monitor's refresh rate) and a vsync option.
- Known limit: game logic stays at 100 Hz and the original renderer has no interpolation. Above roughly 90 fps, frames repeat. Motion is correct and even, but not truly high-refresh. That is a goal for the rewrite.

### Widescreen and display

- Correct the hard-coded 4:3 projection to the real aspect ratio. The vertical field of view stays the same and the horizontal view widens (Hor+).
- Apply the same correction to the 3D menu backgrounds, which use their own scene cameras.
- Re-anchor HUD elements to the screen edges.
- Show menus as a centred 4:3 area, with mouse input mapped correctly.
- Widen the letterbox bars of in-engine cutscenes so they cover the full width.
- Play the FMV videos at 4:3, centred, instead of stretched.
- Any resolution, fullscreen, windowed and borderless windowed, and Alt+Enter to switch.
- Declare the game DPI-aware at start-up. Without this, Windows enlarges the game when display scaling is above 100 % and part of the picture is cut off. The GOG installation avoids this only through a compatibility flag in the registry, which a copied or moved installation doesn't have.

### Launcher and updates

- Portable, no installer: players put the launcher into their Worms Forts folder and start it from there.
- Validate the installation (the hash of `WF.exe`).
- Create and maintain the patched copy of the game executable.
- Detect other mods that patch the same code (for example the Anniversary Patch and its ASI loader) and warn the player.
- Settings: resolution, display mode, frame limit, vsync, language, intro movies, music, control profile.
- Automatic updates from GitHub Releases, verified with our signature. Tauri's built-in updater needs an installer, so the portable launcher needs its own update mechanism.

### Bug fixes

Only stability fixes and clearly broken behaviour. Nothing in 0.1 changes game balance.

- Lua script errors show a message and write a log, instead of silently closing the game.
- Saving writes to a temporary file first, then replaces the old save, and keeps a backup.
- Crash handler that writes a log and a minidump.
- The "Max Wind" Fortpot bug.
- The medal threshold boundary bug.

### Controls

- **Classic profile:** the original feel.
- **Modern profile:**
  - no 300 ms jump wind-up (or a much shorter one), keeping tap, hold and double-tap;
  - walking without the inch-worm pause, with a fast acceleration ramp;
  - precise aiming without drift after releasing the key;
  - no keyboard acceleration ramp and no mouse speed cap.
- **Modern is the default.** Classic can be selected in the launcher.

### Online

- Redirect the GameSpy hostnames to OpenSpy, a community replacement for the defunct GameSpy servers. The Anniversary Patch already does this for most hosts. It misses the presence server (`gpcm`), whose name sits in a writable buffer rather than behind a pointer. Whether OpenSpy fully supports Worms Forts still needs testing.

### Infrastructure

- Logging, a configuration file, and the Classic version shown in the main menu.

## 0.2 and later: Backlog (not yet prioritised)

### AI

- Raise the search limits: longer paths and more plans considered per turn.
- Fix the broken mission scripts.
- Retune the difficulty levels; the hardest levels without random aiming error.
- CPU-only matches to watch (the attract mode already does this).

### Hot-seat and controls

- Gamepad support (XInput), with one gamepad per player.
- Faster hand-over between players: a shorter or skippable hot-seat camera.

### Interface

- In-game video and settings menu.
- HUD scale.
- Optional trajectory preview.
- Clearer display of fort links and territory.

### Graphics (original assets)

- Antialiasing and anisotropic filtering.
- The distance fog that the PC version never drew.

### Balance

- Balance changes through override files: Science Lab stacking, the building destruction threshold, weapons.

### Quality of life

- Skip intros.
- Option to unlock everything.
- Statistics.
- Resume an interrupted match.

### Online

- Direct-IP play.

### Modding

- Shareable override files and custom schemes.

### Platforms

- Test and support running Classic on Linux and macOS through Wine or Proton.
