#pragma once

#include <cstdint>
#include <span>

namespace ofc::patch {

// Each function first checks that the game's code or data at `at` is what we expect. If it is
// not, it changes nothing and returns false, so a different game build is never corrupted.

// Replaces bytes in code or read-only data.
bool replace_bytes(std::uintptr_t at, std::span<const std::uint8_t> expected,
                   std::span<const std::uint8_t> replacement);

// Redirects the relative call at `at` from `expected_target` to `new_target`.
bool redirect_call(std::uintptr_t at, std::uintptr_t expected_target, const void* new_target);

// Replaces instructions (`expected`, at least 5 bytes) with a relative call to `target`, padded
// with NOPs.
bool replace_with_call(std::uintptr_t at, std::span<const std::uint8_t> expected, const void* target);

// Replaces instructions (`expected`, at least 5 bytes) with a relative jump to `target`, padded
// with NOPs. Used to take over a function at its entry.
bool replace_with_jump(std::uintptr_t at, std::span<const std::uint8_t> expected, const void* target);

// Replaces a 32-bit value: a pointer in a table, or a constant or address inside an instruction.
bool replace_dword(std::uintptr_t at, std::uint32_t expected, std::uint32_t value);

}  // namespace ofc::patch
