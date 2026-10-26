#pragma once

namespace ofc::frame_timing {

// Fixes the game clock losing time at high frame rates and limits the frame rate to the
// display's refresh rate. Logs which parts were applied.
void install();

}  // namespace ofc::frame_timing
