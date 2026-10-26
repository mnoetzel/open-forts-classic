#pragma once

#include <cstdint>

namespace ofc::patch {

// Redirects the relative call at `at` from `expected_target` to `new_target`.
// Returns false (and changes nothing) if the call there does not go to `expected_target`.
bool redirect_call(std::uintptr_t at, std::uintptr_t expected_target, const void* new_target);

// Replaces the pointer stored at `at` if it equals `expected`.
// Returns false (and changes nothing) if it holds something else.
bool replace_pointer(std::uintptr_t at, std::uintptr_t expected, const void* replacement);

}  // namespace ofc::patch
