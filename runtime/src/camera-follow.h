#pragma once

namespace ofc::camera_follow {

// Modern profile: when the player turns the third-person camera, it orbits the worm at once instead
// of trailing behind; following the worm's movement stays smoothed. The first-person aim camera
// shows the worm's turn in the same logic step instead of one step late. Logs whether it was applied.
void install();

}  // namespace ofc::camera_follow
