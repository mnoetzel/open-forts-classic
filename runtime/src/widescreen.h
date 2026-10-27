#pragma once

namespace ofc::widescreen {

// Makes the 3D views, the 2D layer and the cutscene bars fit the display's aspect ratio.
// Logs which parts were applied.
void install();

// Moves HUD elements to the screen edges whenever the display's aspect ratio changes.
// Called once per frame.
void update();

}  // namespace ofc::widescreen
