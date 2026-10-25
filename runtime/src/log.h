#pragma once

#include <string_view>

namespace ofc::log {

// Opens OpenForts.log next to the game executable (overwriting the previous one).
void open();

// Appends one line, prefixed with the time since start-up.
void write(std::string_view line);

}  // namespace ofc::log
