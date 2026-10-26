#pragma once

#include <string_view>

namespace ofc::log {

// Opens OpenForts.log next to the game executable (overwriting the previous one). Debug mode is on
// when OpenForts.ini in the game folder contains [Debug] Enabled=1.
void open();

// Appends one line, prefixed with the time since start-up.
void write(std::string_view line);

bool debug_enabled();

// Appends one line marked DEBUG, only in debug mode.
void debug(std::string_view line);

}  // namespace ofc::log
