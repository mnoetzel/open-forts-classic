#pragma once

namespace ofc::mouse_look {

// Modern profile: mouse look follows the mouse exactly. Replaces the game's mouse conversion (8
// counts per logic step at most, with the rest drained over later steps) and the look-axis
// getters, so every consumer keeps the original sensitivity without the speed cap. Logs which parts
// were applied.
void install();

}  // namespace ofc::mouse_look
