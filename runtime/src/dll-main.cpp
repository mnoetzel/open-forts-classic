// OFCRT.DLL, the OpenForts Classic runtime.
//
// The patched game executable (WF-OpenForts.exe) imports this DLL instead of GOG's PATCH.DLL,
// so Windows loads it before any game code runs. PATCH.DLL itself stands in for DINPUT8.DLL and
// exports a single function, DirectInput8Create. We export the same function as a forwarder to
// PATCH.DLL, so the game keeps GOG's fixes until we replace them step by step. Because of the
// forwarder, Windows loads PATCH.DLL at process start like the original WF.exe does, and GOG's
// fixes are in place before any game code runs.

#include <windows.h>

#include <string>

#include "frame-timing.h"
#include "log.h"

#pragma comment(linker, "/EXPORT:DirectInput8Create=patch.DirectInput8Create")

namespace {

std::string executable_path() {
    char path[MAX_PATH];
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    return {path, length};
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        ofc::log::open();
        ofc::log::write("OpenForts Classic runtime " OFC_VERSION " loaded");
        ofc::log::write("Game executable: " + executable_path());
        if (ofc::log::debug_enabled()) {
            ofc::log::write("Debug mode: on");
        }
        ofc::log::write(GetModuleHandleW(L"patch.dll") != nullptr ? "GOG's patch.dll is loaded"
                                                                  : "ERROR: GOG's patch.dll is not loaded");
        // Without this, Windows scales the game when display scaling is above 100 %, which cuts
        // off part of the picture. Must happen before the game creates its window.
        ofc::log::write(SetProcessDPIAware() ? "DPI awareness: enabled"
                                             : "DPI awareness: not changed (already set or not supported)");
        ofc::frame_timing::install();
    } else if (reason == DLL_PROCESS_DETACH) {
        ofc::log::write("Game is shutting down");
    }
    return TRUE;
}
