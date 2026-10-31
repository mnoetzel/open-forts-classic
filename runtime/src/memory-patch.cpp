#include "memory-patch.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace ofc::patch {
namespace {

constexpr std::uint8_t call_opcode = 0xE8;
constexpr std::uint8_t jump_opcode = 0xE9;
constexpr std::uint8_t nop_opcode = 0x90;
constexpr std::size_t call_size = 5;

// A relative call or jump (same size and encoding apart from the opcode).
std::array<std::uint8_t, call_size> relative_branch(std::uint8_t opcode, std::uintptr_t at, std::uintptr_t target) {
    const auto offset = static_cast<std::int32_t>(target - (at + call_size));
    std::array<std::uint8_t, call_size> branch = {opcode};
    std::memcpy(branch.data() + 1, &offset, sizeof offset);
    return branch;
}

std::array<std::uint8_t, call_size> relative_call(std::uintptr_t at, std::uintptr_t target) {
    return relative_branch(call_opcode, at, target);
}

bool replace_with_branch(std::uint8_t opcode, std::uintptr_t at, std::span<const std::uint8_t> expected,
                         const void* target) {
    if (expected.size() < call_size) {
        return false;
    }
    std::vector<std::uint8_t> replacement(expected.size(), nop_opcode);
    const auto branch = relative_branch(opcode, at, reinterpret_cast<std::uintptr_t>(target));
    std::ranges::copy(branch, replacement.begin());
    return replace_bytes(at, expected, replacement);
}

std::array<std::uint8_t, 4> bytes_of(std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes;
    std::memcpy(bytes.data(), &value, sizeof value);
    return bytes;
}

}  // namespace

bool replace_bytes(std::uintptr_t at, std::span<const std::uint8_t> expected,
                   std::span<const std::uint8_t> replacement) {
    auto* address = reinterpret_cast<void*>(at);
    if (expected.size() != replacement.size() || std::memcmp(address, expected.data(), expected.size()) != 0) {
        return false;
    }
    DWORD old_protection = 0;
    VirtualProtect(address, replacement.size(), PAGE_EXECUTE_READWRITE, &old_protection);
    std::memcpy(address, replacement.data(), replacement.size());
    VirtualProtect(address, replacement.size(), old_protection, &old_protection);
    FlushInstructionCache(GetCurrentProcess(), address, replacement.size());
    return true;
}

bool redirect_call(std::uintptr_t at, std::uintptr_t expected_target, const void* new_target) {
    return replace_bytes(at, relative_call(at, expected_target),
                         relative_call(at, reinterpret_cast<std::uintptr_t>(new_target)));
}

bool replace_with_call(std::uintptr_t at, std::span<const std::uint8_t> expected, const void* target) {
    return replace_with_branch(call_opcode, at, expected, target);
}

bool replace_with_jump(std::uintptr_t at, std::span<const std::uint8_t> expected, const void* target) {
    return replace_with_branch(jump_opcode, at, expected, target);
}

bool replace_dword(std::uintptr_t at, std::uint32_t expected, std::uint32_t value) {
    return replace_bytes(at, bytes_of(expected), bytes_of(value));
}

}  // namespace ofc::patch
