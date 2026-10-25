// OFCRT.DLL, the OpenForts Classic runtime.
//
// The patched game executable (WF-OpenForts.exe) imports this DLL instead of GOG's PATCH.DLL,
// so Windows loads it before any game code runs. PATCH.DLL itself stands in for DINPUT8.DLL and
// exports a single function, DirectInput8Create. We export the same function and, for now,
// forward it to PATCH.DLL so the game keeps GOG's fixes until we replace them step by step.

#include <windows.h>
// IUnknown (LPUNKNOWN), which WIN32_LEAN_AND_MEAN leaves out of windows.h.
#include <unknwn.h>

#include <cstdio>
#include <string>

#include "log.h"

namespace {

using DirectInput8CreateFunction = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

std::string hex(HRESULT value) {
    char text[11];
    std::snprintf(text, sizeof text, "0x%08lX", static_cast<unsigned long>(value));
    return text;
}

std::string executable_path() {
    char path[MAX_PATH];
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    return {path, length};
}

// Loaded lazily on the first call: loading libraries inside DllMain can deadlock.
DirectInput8CreateFunction gog_direct_input8_create() {
    static const DirectInput8CreateFunction function = [] {
        HMODULE gog_patch = LoadLibraryW(L"patch.dll");
        if (gog_patch == nullptr) {
            ofc::log::write("ERROR: could not load GOG's patch.dll (error " + std::to_string(GetLastError()) + ")");
            return DirectInput8CreateFunction{nullptr};
        }
        auto* address = reinterpret_cast<DirectInput8CreateFunction>(GetProcAddress(gog_patch, "DirectInput8Create"));
        if (address == nullptr) {
            ofc::log::write("ERROR: patch.dll has no DirectInput8Create export");
        } else {
            ofc::log::write("Loaded GOG's patch.dll; forwarding DirectInput8Create to it");
        }
        return address;
    }();
    return function;
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID interface_id, LPVOID* out,
                                             LPUNKNOWN outer) {
    const DirectInput8CreateFunction next = gog_direct_input8_create();
    if (next == nullptr) {
        return E_FAIL;
    }
    const HRESULT result = next(instance, version, interface_id, out, outer);
    ofc::log::write("DirectInput8Create returned " + hex(result));
    return result;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        ofc::log::open();
        ofc::log::write("OpenForts Classic runtime " OFC_VERSION " loaded");
        ofc::log::write("Game executable: " + executable_path());
    } else if (reason == DLL_PROCESS_DETACH) {
        ofc::log::write("Game is shutting down");
    }
    return TRUE;
}
