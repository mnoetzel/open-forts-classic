#include "log.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace ofc::log {
namespace {

HANDLE file = INVALID_HANDLE_VALUE;
ULONGLONG start_ms = 0;
CRITICAL_SECTION lock;

std::wstring log_path() {
    wchar_t exe[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe, length);
    path.resize(path.find_last_of(L'\\') + 1);
    return path + L"OpenForts.log";
}

}  // namespace

void open() {
    InitializeCriticalSection(&lock);
    start_ms = GetTickCount64();
    file = CreateFileW(log_path().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
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
