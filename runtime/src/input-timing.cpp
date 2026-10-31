#include "input-timing.h"

#include <cstdint>

#include "game-loop.h"
#include "log.h"
#include "memory-patch.h"

// Each frame the game first renders, then runs its update: the due logic steps, then the every-frame
// tasks. InputService (an every-frame task) polls DirectInput, and the devices' messages fill the
// controller records and the mouse counts. So input read in one frame is only used by the logic in
// the next frame and shown in the frame after that. Modern polls the devices at the start of the
// update, before the logic steps, and skips InputService's own poll later in that frame: input is
// used and shown one frame earlier (about 17 ms at 60 Hz). Devices are still polled once per frame.

namespace ofc::input_timing {
namespace {

// InputService::Update (task update, __stdcall this, time; ret 8) and its vtable slot.
constexpr std::uintptr_t input_update_slot = 0x6c27cc;
constexpr std::uintptr_t input_update = 0x5e566f;
// WfApp::Update (__thiscall this, scaled frame time; ret 4), which runs the logic and every-frame
// tasks, and its vtable slot.
constexpr std::uintptr_t app_update_slot = 0x6b5718;
constexpr std::uintptr_t app_update = 0x55c400;

using InputUpdate = std::int32_t(__stdcall*)(void* self, std::uint32_t time);
using AppUpdate = void(__thiscall*)(void* app, std::uint32_t scaled_ms);

void* input_service = nullptr;
std::uint32_t polled_frame = 0;
bool reported = false;

std::int32_t __stdcall input_update_once(void* self, std::uint32_t time) {
    input_service = self;
    if (polled_frame == game_loop::frame_number()) {
        return 0;  // already polled before the logic steps of this frame; 0 keeps it an every-frame task
    }
    polled_frame = game_loop::frame_number();
    return reinterpret_cast<InputUpdate>(input_update)(self, time);
}

// __fastcall with an unused second parameter matches __thiscall.
void __fastcall update_after_polling(void* app, void* /*unused_edx*/, std::uint32_t scaled_ms) {
    if (input_service != nullptr) {
        if (!reported) {
            reported = true;
            log::debug("Input timing: devices are now polled before the logic steps");
        }
        polled_frame = game_loop::frame_number();
        reinterpret_cast<InputUpdate>(input_update)(input_service, 0);  // InputService ignores the time
    }
    reinterpret_cast<AppUpdate>(app_update)(app, scaled_ms);
}

}  // namespace

void install() {
    const bool applied =
        patch::replace_dword(input_update_slot, input_update, reinterpret_cast<std::uintptr_t>(&input_update_once)) &&
        patch::replace_dword(app_update_slot, app_update, reinterpret_cast<std::uintptr_t>(&update_after_polling));
    log::write(applied ? "Controls: input is read before the game logic (one frame less delay)"
                       : "ERROR: unexpected code; input timing was not changed");
}

}  // namespace ofc::input_timing
