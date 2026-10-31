#pragma once

namespace ofc::input_timing {

// Modern profile: reads the input devices before the game logic of each frame instead of after it,
// so mouse and key input reach the game one frame earlier. Logs whether it was applied.
void install();

}  // namespace ofc::input_timing
