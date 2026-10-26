#include "memory-patch.h"

#include <windows.h>

#include <cstring>

namespace ofc::patch {
namespace {

constexpr std::uint8_t call_opcode = 0xE8;
constexpr std::size_t call_size = 5;

// Writes `size` bytes to code or read-only data, restoring the page protection afterwards.
void write(std::uintptr_t at, const void* data, std::size_t size) {
    auto* address = reinterpret_cast<void*>(at);
    DWORD old_protection = 0;
    VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protection);
    std::memcpy(address, data, size);
    VirtualProtect(address, size, old_protection, &old_protection);
    FlushInstructionCache(GetCurrentProcess(), address, size);
}

std::int32_t relative_offset(std::uintptr_t call_at, std::uintptr_t target) {
    return static_cast<std::int32_t>(target - (call_at + call_size));
}

}  // namespace

bool redirect_call(std::uintptr_t at, std::uintptr_t expected_target, const void* new_target) {
    std::uint8_t expected[call_size] = {call_opcode};
    const std::int32_t expected_offset = relative_offset(at, expected_target);
    std::memcpy(expected + 1, &expected_offset, sizeof expected_offset);
    if (std::memcmp(reinterpret_cast<const void*>(at), expected, call_size) != 0) {
        return false;
    }
    const std::int32_t new_offset = relative_offset(at, reinterpret_cast<std::uintptr_t>(new_target));
    write(at + 1, &new_offset, sizeof new_offset);
    return true;
}

bool replace_pointer(std::uintptr_t at, std::uintptr_t expected, const void* replacement) {
    if (*reinterpret_cast<const std::uintptr_t*>(at) != expected) {
        return false;
    }
    const auto value = reinterpret_cast<std::uintptr_t>(replacement);
    write(at, &value, sizeof value);
    return true;
}

}  // namespace ofc::patch
