#pragma once

namespace ofc::frame_timing {

// Fixes the game clock losing time at high frame rates. Logs whether it was applied.
void install();

// Frame limiter: waits until the next frame is due at the display's refresh rate.
void wait_for_next_frame();

}  // namespace ofc::frame_timing
