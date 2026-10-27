#include "log.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace ofc::log {
namespace {

HANDLE file = INVALID_HANDLE_VALUE;
ULONGLONG start_ms = 0;
CRITICAL_SECTION lock;
bool debug_mode = false;

std::wstring game_folder() {
    wchar_t exe[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe, length);
    path.resize(path.find_last_of(L'\\') + 1);
    return path;
}

// OpenForts.ini in the game folder, written by the launcher.
bool debug_requested() {
    return GetPrivateProfileIntW(L"Debug", L"Enabled", 0, (game_folder() + L"OpenForts.ini").c_str()) != 0;
}

}  // namespace

void open() {
    InitializeCriticalSection(&lock);
    start_ms = GetTickCount64();
    file = CreateFileW((game_folder() + L"OpenForts.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    debug_mode = debug_requested();
}

bool debug_enabled() { return debug_mode; }

void debug(std::string_view line) {
    if (debug_mode) {
        write(std::string("DEBUG ").append(line));
    }
}

void write(std::string_view line) {
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    const ULONGLONG elapsed = GetTickCount64() - start_ms;
    char prefix[32];
    const int prefix_length = std::snprintf(prefix, sizeof prefix, "[%7llu.%03llu] ", elapsed / 1000, elapsed % 1000);

    std::string text(prefix, static_cast<size_t>(prefix_length));
    text.append(line);
    text.append("\r\n");

    EnterCriticalSection(&lock);
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    LeaveCriticalSection(&lock);
}

}  // namespace ofc::log
