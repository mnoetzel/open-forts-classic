#pragma once

// Debug mode only: traces how mouse input moves the cursor in the weapons drawer (per drawer update,
// and the drawn pointer per frame) and how first-person aiming follows the mouse (per camera update).
namespace ofc::input_trace {

void install();

// Called once per rendered frame; does nothing unless the trace is installed.
void update();

}  // namespace ofc::input_trace
