#pragma once

namespace ofc::controls {

// Applies the code changes of the Modern controls profile (unless the Classic profile is chosen).
// Logs which parts were applied.
void install();

// Applies the Modern values to the game's control and camera data once it is loaded, and again if
// the game reloads it. Called once per frame.
void update();

}  // namespace ofc::controls
