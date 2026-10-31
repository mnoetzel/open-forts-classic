#include "settings.h"

#include <windows.h>

namespace ofc::settings {
namespace {

std::wstring ini_path() { return game_folder() + L"OpenForts.ini"; }

}  // namespace

std::wstring game_folder() {
    wchar_t exe[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe, length);
    path.resize(path.find_last_of(L'\\') + 1);
    return path;
}

bool debug_enabled() { return GetPrivateProfileIntW(L"Debug", L"Enabled", 0, ini_path().c_str()) != 0; }

ControlsProfile controls_profile() {
    wchar_t value[16] = {};
    GetPrivateProfileStringW(L"Controls", L"Profile", L"Modern", value, static_cast<DWORD>(std::size(value)),
                             ini_path().c_str());
    return CompareStringOrdinal(value, -1, L"Classic", -1, TRUE) == CSTR_EQUAL ? ControlsProfile::classic
                                                                               : ControlsProfile::modern;
}

}  // namespace ofc::settings
