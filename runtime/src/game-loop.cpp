#include "game-loop.h"

#include <cstdint>
#include <iterator>
#include <string>

#include "frame-timing.h"
#include "log.h"
#include "memory-patch.h"
#include "widescreen.h"

namespace ofc::game_loop {
namespace {

// XomTaskAppBase::Frame(unsigned dt_ms) in WF.exe (GOG build) and the vtable slots that point to it.
constexpr std::uintptr_t frame_function = 0x5e1885;
constexpr std::uintptr_t frame_vtable_slots[] = {0x6b5714, 0x6c22b4, 0x6c235c};

using FrameFunction = void(__thiscall*)(void* app, unsigned dt_ms);

// Replaces Frame in the vtables. __fastcall with an unused second parameter matches __thiscall.
void __fastcall frame(void* app, void* /*unused_edx*/, unsigned dt_ms) {
    widescreen::update();
    reinterpret_cast<FrameFunction>(frame_function)(app, dt_ms);
    frame_timing::wait_for_next_frame();
}

}  // namespace

void install() {
    int replaced = 0;
    for (const std::uintptr_t slot : frame_vtable_slots) {
        replaced += patch::replace_dword(slot, frame_function, reinterpret_cast<std::uintptr_t>(&frame));
    }
    log::write("Game loop: frame hook installed in " + std::to_string(replaced) + " of " +
               std::to_string(std::size(frame_vtable_slots)) + " vtables");
}

}  // namespace ofc::game_loop
