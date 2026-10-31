#pragma once

#include <string>

// Settings from OpenForts.ini in the game folder, written by the launcher.
namespace ofc::settings {

enum class ControlsProfile { modern, classic };

// The folder of the game executable, with a trailing backslash.
std::wstring game_folder();

// [Debug] Enabled=1
bool debug_enabled();

// [Controls] Profile=Modern|Classic; Modern unless the file says Classic.
ControlsProfile controls_profile();

}  // namespace ofc::settings
