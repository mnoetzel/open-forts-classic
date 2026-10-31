#include "controls.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "camera-follow.h"
#include "game-loop.h"
#include "input-timing.h"
#include "log.h"
#include "memory-patch.h"
#include "mouse-look.h"
#include "settings.h"

// The Modern controls profile removes the sluggishness of the original controls while keeping its
// mechanics: worms keep their inch-worm walk, and the jump keeps its original 300 ms wind-up, which is
// also the window that tells tap (forward jump), hold (high jump) and double tap (backflip) apart; a
// shorter window makes the double tap impossible. The Classic profile leaves everything as it was.
// The values feed into the synchronised game simulation, so LAN players must use the same profile.

namespace ofc::controls {
namespace {

bool modern = false;

// --- Control and camera data --------------------------------------------------------------------
// WormControlContainer (Control.xom) and CameraControlContainer (Camera.xom) are shared by all
// worms and read live every step. Their pointers are in the tweak data slots: *(0x750910) + 0x24
// and + 0x2c.

constexpr std::uintptr_t tweak_slots_pointer = 0x750910;
constexpr std::uintptr_t worm_control_slot = 0x24;
constexpr std::uintptr_t camera_control_slot = 0x2c;

struct DataValue {
    const char* name;
    std::uintptr_t offset;
    float original;
    float modern;
};

// The original gait cycle is 5000 counter units; Modern shortens it to 4375 (see below). To keep
// the average walking speed, WalkSpeed shrinks by the same ratio.
constexpr float walk_speed_ratio = 4375.0f / 5000.0f;

// Turning, aiming and camera rotation go through a rate smoother (FUN_0058ff80) every logic step:
// the rate moves towards the input by at most "maximum rate / (time to max speed * 100)" per step,
// and is multiplied by "when stopping multiply" in steps without input, so it ramps up and coasts.
// Modern makes them instant for every input device: a ramp time of 1 ms (a tenth of a step) lets
// the rate reach any input in one step, and a stopping multiplier of 0 stops it in the first step
// without input. With the mouse this needs the exact per-step movement of mouse-look.cpp; with the
// original drained mouse input, steps without new movement would stutter. Maximum speeds stay
// original, so keys and sticks turn as fast as before.
constexpr float instant_ramp_seconds = 0.001f;

constexpr std::array worm_values = {
    DataValue{"MaxAcceleration", 0x5c, 0.01f, 1.0f / 15.0f},  // full walking speed after 0.15 s
    DataValue{"WalkSpeed", 0x60, 1.55f, 1.55f * walk_speed_ratio},
    DataValue{"AimTimeToMaxSpeed", 0xd4, 0.5f, instant_ramp_seconds},
    DataValue{"AimWhenStoppingMultiply", 0x48, 0.9f, 0.0f},
    DataValue{"StationaryTurnTimeToMaxSpeed", 0x40, 0.5f, instant_ramp_seconds},
    DataValue{"StationaryWhenStoppingMultiply", 0x3c, 0.9f, 0.0f},
};

constexpr std::array camera_values = {
    DataValue{"DefaultTimeToMaxYaw", 0x78, 0.3f, instant_ramp_seconds},
    DataValue{"DefaultWhenStoppingMultiplyYaw", 0x74, 0.5f, 0.0f},
    DataValue{"DefaultTimeToMaxPitch", 0x70, 0.4f, instant_ramp_seconds},
    DataValue{"DefaultWhenStoppingMultiplyPitch", 0x6c, 0.4f, 0.0f},
    DataValue{"DefaultPositionInertia", 0x90, 0.95f, 0.85f},  // following the worm lags about 0.06 s instead of 0.2 s
    DataValue{"DefaultLookAtInertia", 0x170, 0.95f, 0.85f},
};

// Writes the Modern value wherever the original value is found, so data the game (re)loads is
// covered. Values that are neither are left alone and reported once (bit per value in `reported`).
template <std::size_t count>
void apply(const char* container_name, std::uintptr_t container, const std::array<DataValue, count>& values,
           std::uint32_t& reported) {
    static_assert(count <= 32);
    for (std::size_t index = 0; index < count; ++index) {
        const DataValue& value = values[index];
        float& field = *reinterpret_cast<float*>(container + value.offset);
        if (field == value.original) {
            field = value.modern;
            log::debug(std::string(container_name) + "." + value.name + ": " + std::to_string(value.original) + " -> " +
                       std::to_string(value.modern));
        } else if (field != value.modern && (reported & (1u << index)) == 0) {
            reported |= 1u << index;
            log::write("ERROR: unexpected " + std::string(container_name) + "." + value.name + " (" +
                       std::to_string(field) + "); it was not changed");
        }
    }
}

std::uintptr_t tweak_container(std::uintptr_t slot) {
    const auto slots = *reinterpret_cast<const std::uintptr_t*>(tweak_slots_pointer);
    return slots == 0 ? 0 : *reinterpret_cast<const std::uintptr_t*>(slots + slot);
}

// --- Keyboard ramp ------------------------------------------------------------------------------
// LogicControlService::Update (0x569830) smooths digital axes (keyboard) by 4/127 per step up and
// 8/127 down, unless the axis is analog or bound to the mouse. For each of the six axes it first
// tests the player's IsHuman flag (PlayerDataContainer +0x123): `test cl, cl; je <use raw value>`.
// Turning these six `je` into `jmp` makes every axis use the raw value, like a stick or the mouse.

struct RawAxisJump {
    std::uintptr_t at;
    std::uint8_t displacement;
};

constexpr std::array raw_axis_jumps = {
    RawAxisJump{0x569ee6, 0x27}, RawAxisJump{0x569f22, 0x24}, RawAxisJump{0x569f5b, 0x24},
    RawAxisJump{0x569f91, 0x24}, RawAxisJump{0x569fca, 0x27}, RawAxisJump{0x56a006, 0x27},
};

constexpr std::uint8_t je_short = 0x74;
constexpr std::uint8_t jmp_short = 0xeb;

bool remove_keyboard_ramp() {
    bool all = true;
    for (const RawAxisJump& jump : raw_axis_jumps) {
        const std::array<std::uint8_t, 2> original = {je_short, jump.displacement};
        const std::array<std::uint8_t, 2> replacement = {jmp_short, jump.displacement};
        all = patch::replace_bytes(jump.at, original, replacement) && all;
    }
    return all;
}

// --- Gait ---------------------------------------------------------------------------------------
// The worm's gait counter (+0x298) wraps at 5000 (`cmp eax, 0x1388` at 0x582305). Below 3750 the
// worm moves with a sine speed profile, from 3750 to the wrap it stands still (the inch-worm pause,
// 25 % of the cycle), and the pause animation runs at (counter - 3750) / 1250. Modern halves the
// pause: the counter wraps at 4375 and the pause animation runs at (counter - 3750) / 625. The
// factor 1/1250 (0x6bdc50) is shared with unrelated code, so the instruction that uses it for the
// pause animation (0x58235e) is pointed at our own value instead.

constexpr std::uintptr_t gait_wrap_operand = 0x582306;
constexpr std::uint32_t original_gait_wrap = 5000;
constexpr std::uint32_t modern_gait_wrap = 4375;

constexpr std::uintptr_t pause_animation_factor_operand = 0x58235e;
constexpr std::uintptr_t original_pause_animation_factor = 0x6bdc50;
const float modern_pause_animation_factor = 1.0f / static_cast<float>(modern_gait_wrap - 3750);

// Both parts or neither: a shorter cycle without the faster pause animation would not match.
bool shorten_gait_pause() {
    const bool originals_found =
        *reinterpret_cast<const std::uint32_t*>(gait_wrap_operand) == original_gait_wrap &&
        *reinterpret_cast<const std::uintptr_t*>(pause_animation_factor_operand) == original_pause_animation_factor;
    return originals_found && patch::replace_dword(gait_wrap_operand, original_gait_wrap, modern_gait_wrap) &&
           patch::replace_dword(pause_animation_factor_operand, original_pause_animation_factor,
                                reinterpret_cast<std::uintptr_t>(&modern_pause_animation_factor));
}

// --- Weapon aim smoothing -----------------------------------------------------------------------
// Weapons on the fort platforms aim with their own code (FUN_00469bb0), not with the worm's data:
// it calls the rate smoother FUN_0058ff80 for yaw (0x469d73) and pitch (0x469d9b) with a ramp of
// 0.5 s and a stopping multiplier of 0.9, both fixed in the code, so the aim ramps up slowly and
// overshoots after the input stops. Instant start and stop felt too abrupt for aiming, so Modern
// replaces both calls with a short exponential smoothing: the rate follows the input with a time
// constant of about 50 ms at the start and the end. Unlike the original smoother this never adds
// movement: the total aim change equals the input (keys held for a time, or the mouse movement).

constexpr std::uintptr_t rate_smoother = 0x58ff80;
constexpr std::array<std::uintptr_t, 2> weapon_aim_smoother_calls = {0x469d73, 0x469d9b};
constexpr float weapon_aim_follow_per_step = 0.18f;  // 1 - e^(-10 ms / 50 ms)

// Replaces FUN_0058ff80 (__thiscall rate, target, maximum rate, ramp steps, stopping multiplier;
// ret 0x10). __fastcall with an unused second parameter matches __thiscall.
void __fastcall smooth_weapon_aim(float* rate, void* /*unused_edx*/, float target, float /*maximum_rate*/,
                                  float /*ramp_steps*/, float /*stopping_multiplier*/) {
    *rate += (target - *rate) * weapon_aim_follow_per_step;
}

bool smooth_weapon_aim_briefly() {
    // Both or neither: yaw and pitch should feel the same. Each site must be `call rate_smoother`.
    const bool originals_found = std::ranges::all_of(weapon_aim_smoother_calls, [](std::uintptr_t call) {
        constexpr std::uint8_t call_opcode = 0xe8;
        constexpr std::uintptr_t call_size = 5;
        return *reinterpret_cast<const std::uint8_t*>(call) == call_opcode &&
               call + call_size + *reinterpret_cast<const std::int32_t*>(call + 1) == rate_smoother;
    });
    if (!originals_found) {
        return false;
    }
    bool all = true;
    for (const std::uintptr_t call : weapon_aim_smoother_calls) {
        all = patch::redirect_call(call, rate_smoother, &smooth_weapon_aim) && all;
    }
    return all;
}

// --- Weapons drawer cursor stays on screen (bug fix, both profiles) -------------------------------
// The drawer moves its own cursor (+0x54/+0x58) with the mouse (FUN_004631a0) and passes it to the
// mouse pointer, which is clamped to the screen (FUN_0049f770, limits 0x7311f4..0x731204). The
// drawer's own cursor is never clamped: a fast movement carries it far off screen, the pointer
// sticks at the edge, and the mouse has to travel all the way back before the cursor moves again.
// Before the drawer selects the cell under its cursor (call to FUN_004623d0 at 0x46320d, drawer in
// esi), the cursor is now clamped to the same limits.

constexpr std::uintptr_t drawer_select_call = 0x46320d;
const std::uintptr_t drawer_select_function = 0x4623d0;  // not constexpr: used from inline assembler

constexpr std::uintptr_t drawer_cursor_x = 0x54;
constexpr std::uintptr_t drawer_cursor_y = 0x58;
constexpr std::uintptr_t mouse_limit_left = 0x7311f4;
constexpr std::uintptr_t mouse_limit_bottom = 0x7311f8;
constexpr std::uintptr_t mouse_limit_right = 0x731200;
constexpr std::uintptr_t mouse_limit_top = 0x731204;

float limit(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }

std::uint32_t drawer_clamp_log_frame = 0;

void __stdcall keep_drawer_cursor_on_screen(std::byte* drawer) {
    auto& x = *reinterpret_cast<float*>(drawer + drawer_cursor_x);
    auto& y = *reinterpret_cast<float*>(drawer + drawer_cursor_y);
    const float clamped_x = std::clamp(x, limit(mouse_limit_left), limit(mouse_limit_right));
    const float clamped_y = std::clamp(y, limit(mouse_limit_bottom), limit(mouse_limit_top));
    if ((clamped_x != x || clamped_y != y) && game_loop::frame_number() - drawer_clamp_log_frame > 60) {
        drawer_clamp_log_frame = game_loop::frame_number();
        log::debug("Drawer cursor kept on screen: (" + std::to_string(x) + ", " + std::to_string(y) + ") -> (" +
                   std::to_string(clamped_x) + ", " + std::to_string(clamped_y) + ")");
    }
    x = clamped_x;
    y = clamped_y;
}

// Keeps every register (the selection function reads the drawer from esi), then selects.
__declspec(naked) void clamp_drawer_cursor_then_select() {
    __asm {
        push eax
        push ecx
        push edx
        push esi
        call keep_drawer_cursor_on_screen
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [drawer_select_function]
    }
}

// --- Weapons drawer cursor speed ----------------------------------------------------------------
// The menus move the mouse pointer by one unit per mouse count (FUN_0049fad0). The weapons drawer
// moves its cursor by the look axis value times 16 (FUN_004631a0: `fmul [0x6f6e90]` for x at 0x4631f1
// and for y at 0x463201), and 7.2 counts make one axis unit, so the drawer cursor moves about 2.2
// units per count: small movements jump by several pixels. Modern points both multiplications at a
// factor of 7.2, so the drawer cursor moves like the menu pointer. The constant 16 is shared with
// other code, so only these two instructions change.

constexpr std::array<std::uintptr_t, 2> drawer_speed_operands = {0x4631f3, 0x463203};
constexpr std::uintptr_t original_drawer_speed = 0x6f6e90;
const float modern_drawer_speed = 7.2f;

// Both or neither: x and y should move alike.
bool match_drawer_speed_to_menus() {
    const bool originals_found = std::ranges::all_of(drawer_speed_operands, [](std::uintptr_t operand) {
        return *reinterpret_cast<const std::uintptr_t*>(operand) == original_drawer_speed;
    });
    if (!originals_found) {
        return false;
    }
    bool all = true;
    for (const std::uintptr_t operand : drawer_speed_operands) {
        all = patch::replace_dword(operand, original_drawer_speed,
                                   reinterpret_cast<std::uintptr_t>(&modern_drawer_speed)) &&
              all;
    }
    return all;
}

// --- Camera pitch direction (bug fix, both profiles) --------------------------------------------
// Camera pitch (FUN_00593ad0, used by the worm camera and weapon cameras) and aim pitch apply the
// "Invert Y" option the same way, but the two angles point in opposite directions: a larger camera
// pitch looks down more steeply, a larger aim pitch aims higher. So the same mouse movement turned
// the camera and the aim in opposite directions, whatever the option. Swapping the camera's
// `je` (subtract without Invert Y) for `jne` at 0x593b39 makes the camera follow the aim.

constexpr std::uintptr_t camera_pitch_direction_jump = 0x593b39;
constexpr std::array<std::uint8_t, 2> camera_pitch_je = {0x74, 0x04};
constexpr std::array<std::uint8_t, 2> camera_pitch_jne = {0x75, 0x04};

void log_result(bool applied, const std::string& part) {
    log::write(applied ? "Controls: " + part : "ERROR: unexpected code; controls " + part + " was not applied");
}

}  // namespace

void install() {
    log_result(patch::replace_bytes(camera_pitch_direction_jump, camera_pitch_je, camera_pitch_jne),
               "camera pitch follows the aim direction (Invert Y applies to both)");
    log_result(patch::redirect_call(drawer_select_call, drawer_select_function, &clamp_drawer_cursor_then_select),
               "weapons drawer cursor stays on screen");

    modern = settings::controls_profile() == settings::ControlsProfile::modern;
    log::write(modern ? "Controls: Modern profile" : "Controls: Classic profile (original controls)");
    if (!modern) {
        return;
    }
    log_result(remove_keyboard_ramp(), "keyboard ramp removed");
    log_result(shorten_gait_pause(), "inch-worm pause halved");
    log_result(smooth_weapon_aim_briefly(), "weapon aiming smoothed briefly at start and stop");
    log_result(match_drawer_speed_to_menus(), "weapons drawer cursor moves like the menu pointer");
    mouse_look::install();
    camera_follow::install();
    input_timing::install();
}

void update() {
    if (!modern) {
        return;
    }
    static std::uint32_t worm_reported = 0;
    static std::uint32_t camera_reported = 0;
    if (const std::uintptr_t worm = tweak_container(worm_control_slot); worm != 0) {
        apply("WormControl", worm, worm_values, worm_reported);
    }
    if (const std::uintptr_t camera = tweak_container(camera_control_slot); camera != 0) {
        apply("CameraControl", camera, camera_values, camera_reported);
    }
}

}  // namespace ofc::controls
