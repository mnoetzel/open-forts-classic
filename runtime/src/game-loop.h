#pragma once

#include <cstdint>

namespace ofc::game_loop {

// Wraps the game's per-frame function, so our per-frame work runs around every frame.
void install();

// The number of rendered frames so far.
std::uint32_t frame_number();

}  // namespace ofc::game_loop
