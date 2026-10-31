#include "input-trace.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <string>

#include "game-loop.h"
#include "log.h"
#include "memory-patch.h"

// Drawer trace, one line per update of the weapons drawer while it is open and something happens:
// - f / s: the rendered frame and the number of logic steps run so far in that frame
// - mouse: LogicControlService's accumulated mouse counts (x, y) and how much of them the logic step
//   consumed before -> after
// - mousePath: the (x, y) axis values passed to the direct mouse movement (FUN_004631a0), if called
// - gridPath: the (x, y) values passed to the cell-by-cell movement (FUN_00462f90), if called
// - cursor: the cursor position before -> after the update; sel: the selected cell; grid: the
//   cell-stepping counters; open: the drawer state (0 closed, 50 open); limits: the mouse limits in x
//
// Pointer trace, one line per rendered frame while the drawer is open and the pointer has moved: the
// position of the mouse pointer that is drawn (what the player sees), and the logic steps of the
// previous frame.
//
// Aim trace, one line per camera update in first-person aim (Aim held, FUN_0058fa90):
// - f / s / t: frame, logic steps so far in that frame, camera update time (ms)
// - counts: the mouse counts this logic step used (x, y)
// - look raw / smooth: the look bytes (x, y) of the player's controller record
// - worm: turn rate and pitch rate (degrees per step), aim pitch (degrees), state and flags
// - cam: the camera's yaw (degrees, from the worm's orientation), its change since the previous
//   line, pitch (degrees, from the look direction) and position
// - weapon: the selected weapon's aim yaw and pitch (degrees), aim mode and data flags
//   (+0x392, +0x396, +0x39e; their meaning is not verified yet), or "none"

namespace ofc::input_trace {
namespace {

constexpr int max_lines = 6000;

// LogicControlService::Update (0x569830), a virtual method (this, argument), ret 8.
constexpr std::uintptr_t logic_control_update_slot = 0x6bc250;
constexpr std::uintptr_t logic_control_update = 0x569830;
// The weapons drawer's input update (0x461d00), a virtual method (this, dt), ret 8.
constexpr std::uintptr_t drawer_update_slot = 0x6e7450;
constexpr std::uintptr_t drawer_update = 0x461d00;
// Inside the drawer update: the direct mouse movement (this in eax; x, y, time; ret 0xc) and the
// cell-by-cell movement (this in esi; x, y; ret 8).
constexpr std::uintptr_t mouse_path_call = 0x46215d;
const std::uintptr_t mouse_path_function = 0x4631a0;  // not constexpr: used from inline assembler
constexpr std::uintptr_t grid_path_call = 0x46226d;
const std::uintptr_t grid_path_function = 0x462f90;  // not constexpr: used from inline assembler

// LogicControlService: accumulated mouse counts and the consumed part.
constexpr std::uintptr_t mouse_accumulated_x = 0x42c;
constexpr std::uintptr_t mouse_accumulated_y = 0x430;
constexpr std::uintptr_t mouse_consumed_x = 0x438;
constexpr std::uintptr_t mouse_consumed_y = 0x43c;
// Weapons drawer input object.
constexpr std::uintptr_t cursor_x = 0x54;
constexpr std::uintptr_t cursor_y = 0x58;
constexpr std::uintptr_t grid_counter_x = 0x6c;
constexpr std::uintptr_t grid_counter_y = 0x70;
constexpr std::uintptr_t selected_cell = 0x78;
constexpr std::uintptr_t drawer_state = 0x80;
// Mouse limit table.
constexpr std::uintptr_t mouse_limit_left = 0x7311f4;
constexpr std::uintptr_t mouse_limit_right = 0x731200;
// The mouse pointer entity (global) and its position.
constexpr std::uintptr_t mouse_pointer = 0x750e68;
constexpr std::uintptr_t pointer_x = 0x24;
constexpr std::uintptr_t pointer_y = 0x28;

// zCameraLogicEntity::Update (0x59c0c0), a virtual method (this, time), ret 8, and the follow mode's
// call of the first-person aim mode (0x58fa90, __thiscall camera, result; ret 4) at 0x59b531. The
// Modern profile may already have redirected that call (camera-follow.cpp); the trace then wraps
// the redirected function, so it logs what the player sees.
constexpr std::uintptr_t camera_update_slot = 0x6bdf44;
constexpr std::uintptr_t camera_update = 0x59c0c0;
constexpr std::uintptr_t first_person_call = 0x59b531;
std::uintptr_t first_person_function = 0;  // the call's target when the trace was installed
// Camera fields: target list (first entry = main target), position, look-at, yaw.
constexpr std::uintptr_t camera_targets = 0x3c;
constexpr std::uintptr_t camera_position = 0x44;
constexpr std::uintptr_t camera_look_at = 0x74;
constexpr std::uintptr_t camera_yaw = 0x1e0;
// Entity type (1 = worm).
constexpr std::uintptr_t entity_type = 0xe4;
constexpr std::int32_t worm_type = 1;
// Worm fields: player, turn rate, aim pitch, pitch rate, state, flags.
constexpr std::uintptr_t worm_player = 0x11c;
constexpr std::uintptr_t worm_turn_rate = 0x1b4;
constexpr std::uintptr_t worm_aim_pitch = 0x198;
constexpr std::uintptr_t worm_pitch_rate = 0x1b8;
constexpr std::uintptr_t worm_state = 0x148;
constexpr std::uintptr_t worm_flags = 0x138;
// Player: PlayerDataContainer (controller index at +0x84) and selected weapon.
constexpr std::uintptr_t player_data = 0x98;
constexpr std::uintptr_t player_controller = 0x84;
constexpr std::uintptr_t player_weapon = 0xa0;
// LogicControlService: controller records, raw at +0x59 and smoothed at +0xc2, 8 bytes each; the
// look bytes are 6 (x) and 7 (y).
constexpr std::uintptr_t logic_control_service = 0x738994;
constexpr std::uintptr_t raw_records = 0x59;
constexpr std::uintptr_t smoothed_records = 0xc2;
// Weapon: aim yaw, aim pitch, aim mode, data (WeaponDataContainer) with three flags.
constexpr std::uintptr_t weapon_yaw = 0x1c4;
constexpr std::uintptr_t weapon_pitch = 0x1cc;
constexpr std::uintptr_t weapon_aim_mode = 0x1a4;
constexpr std::uintptr_t weapon_data = 0x17c;
constexpr std::array<std::uintptr_t, 3> weapon_flags = {0x392, 0x396, 0x39e};

constexpr float degrees_per_radian = 180.0f / std::numbers::pi_v<float>;

template <typename T>
T field(const void* object, std::uintptr_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const std::byte*>(object) + offset);
}

bool trace_installed = false;

// Filled by the call-site stubs during one drawer update.
float mouse_path_x = 0.0f;
float mouse_path_y = 0.0f;
std::uint32_t mouse_path_calls = 0;
float grid_path_x = 0.0f;
float grid_path_y = 0.0f;
std::uint32_t grid_path_calls = 0;

// Mouse state around the latest logic step.
struct MouseSnapshot {
    float accumulated_x = 0.0f;
    float accumulated_y = 0.0f;
    float consumed_x_before = 0.0f;
    float consumed_y_before = 0.0f;
    float consumed_x_after = 0.0f;
    float consumed_y_after = 0.0f;
};
MouseSnapshot mouse;
std::uint32_t step_frame = 0;
int steps_in_frame = 0;
int lines = 0;
std::uint32_t drawer_open_frame = 0;

using LogicControlUpdate = std::int32_t(__stdcall*)(void* self, std::uint32_t argument);
using DrawerUpdate = std::uint32_t(__stdcall*)(void* self, float dt);

std::int32_t __stdcall traced_logic_control_update(void* self, std::uint32_t argument) {
    const std::uint32_t frame = game_loop::frame_number();
    if (frame != step_frame) {
        step_frame = frame;
        steps_in_frame = 0;
    }
    ++steps_in_frame;
    mouse.consumed_x_before = field<float>(self, mouse_consumed_x);
    mouse.consumed_y_before = field<float>(self, mouse_consumed_y);
    const std::int32_t result = reinterpret_cast<LogicControlUpdate>(logic_control_update)(self, argument);
    mouse.accumulated_x = field<float>(self, mouse_accumulated_x);
    mouse.accumulated_y = field<float>(self, mouse_accumulated_y);
    mouse.consumed_x_after = field<float>(self, mouse_consumed_x);
    mouse.consumed_y_after = field<float>(self, mouse_consumed_y);
    return result;
}

std::uint32_t __stdcall traced_drawer_update(void* self, float dt) {
    mouse_path_calls = 0;
    grid_path_calls = 0;
    const float x_before = field<float>(self, cursor_x);
    const float y_before = field<float>(self, cursor_y);
    const int selected_before = field<int>(self, selected_cell);
    const float grid_x_before = field<float>(self, grid_counter_x);
    const float grid_y_before = field<float>(self, grid_counter_y);

    const std::uint32_t result = reinterpret_cast<DrawerUpdate>(drawer_update)(self, dt);

    const int state = field<int>(self, drawer_state);
    if (state != 0) {
        drawer_open_frame = game_loop::frame_number();
    }
    const float x_after = field<float>(self, cursor_x);
    const float y_after = field<float>(self, cursor_y);
    const int selected_after = field<int>(self, selected_cell);
    const bool mouse_moved =
        mouse.consumed_x_after != mouse.consumed_x_before || mouse.consumed_y_after != mouse.consumed_y_before;
    const bool cursor_moved = x_after != x_before || y_after != y_before || selected_after != selected_before;
    const bool path_input = (mouse_path_calls != 0 && (mouse_path_x != 0.0f || mouse_path_y != 0.0f)) ||
                            (grid_path_calls != 0 && (grid_path_x != 0.0f || grid_path_y != 0.0f));
    if (state == 0 || !(mouse_moved || cursor_moved || path_input) || lines > max_lines) {
        return result;
    }
    if (++lines > max_lines) {
        log::debug("Drawer trace: limit reached, no more lines");
        return result;
    }
    char text[512];
    std::snprintf(text, sizeof text,
                  "Drawer trace f=%u s=%d dt=%.4f open=%d mouse=(%.1f,%.1f) consumed x %.1f->%.1f y %.1f->%.1f "
                  "mousePath%s=(%.3f,%.3f) gridPath%s=(%.3f,%.3f) cursor=(%.2f,%.2f)->(%.2f,%.2f) sel=%d->%d "
                  "grid=(%.2f,%.2f)->(%.2f,%.2f) limits=[%.1f,%.1f]",
                  step_frame, steps_in_frame, dt, state, mouse.accumulated_x, mouse.accumulated_y,
                  mouse.consumed_x_before, mouse.consumed_x_after, mouse.consumed_y_before, mouse.consumed_y_after,
                  mouse_path_calls != 0 ? "" : "(not called)", mouse_path_x, mouse_path_y,
                  grid_path_calls != 0 ? "" : "(not called)", grid_path_x, grid_path_y, x_before, y_before, x_after,
                  y_after, selected_before, selected_after, grid_x_before, grid_y_before,
                  field<float>(self, grid_counter_x), field<float>(self, grid_counter_y),
                  *reinterpret_cast<const float*>(mouse_limit_left),
                  *reinterpret_cast<const float*>(mouse_limit_right));
    log::debug(text);
    return result;
}

// Call-site stubs: copy the two axis arguments, keep every register, continue in the original.
__declspec(naked) void traced_mouse_path() {
    __asm {
        push edx
        mov edx, dword ptr [esp + 8]  // x (after the pushed edx and the return address)
        mov dword ptr [mouse_path_x], edx
        mov edx, dword ptr [esp + 12]  // y
        mov dword ptr [mouse_path_y], edx
        pop edx
        inc dword ptr [mouse_path_calls]
        jmp dword ptr [mouse_path_function]
    }
}

__declspec(naked) void traced_grid_path() {
    __asm {
        push edx
        mov edx, dword ptr [esp + 8]  // x
        mov dword ptr [grid_path_x], edx
        mov edx, dword ptr [esp + 12]  // y
        mov dword ptr [grid_path_y], edx
        pop edx
        inc dword ptr [grid_path_calls]
        jmp dword ptr [grid_path_function]
    }
}

// --- Pointer trace ------------------------------------------------------------------------------

float last_pointer_x = 0.0f;
float last_pointer_y = 0.0f;
int pointer_lines = 0;

void trace_pointer() {
    const std::uint32_t frame = game_loop::frame_number();
    if (drawer_open_frame == 0 || frame - drawer_open_frame > 1 || pointer_lines > max_lines) {
        return;
    }
    const auto* pointer = *reinterpret_cast<const std::byte* const*>(mouse_pointer);
    if (pointer == nullptr) {
        return;
    }
    const float x = field<float>(pointer, pointer_x);
    const float y = field<float>(pointer, pointer_y);
    if (x == last_pointer_x && y == last_pointer_y) {
        return;
    }
    if (++pointer_lines > max_lines) {
        log::debug("Pointer trace: limit reached, no more lines");
        return;
    }
    char text[256];
    std::snprintf(text, sizeof text, "Pointer trace f=%u pointer=(%.2f,%.2f) moved=(%.2f,%.2f) steps in f=%u: %d",
                  frame, x, y, x - last_pointer_x, y - last_pointer_y, step_frame,
                  step_frame == frame - 1 ? steps_in_frame : 0);
    log::debug(text);
    last_pointer_x = x;
    last_pointer_y = y;
}

// --- Aim trace ----------------------------------------------------------------------------------

bool first_person_ran = false;
float previous_camera_yaw = 0.0f;
int aim_lines = 0;

using FirstPerson = std::uint32_t(__thiscall*)(void* camera, void* result);
using CameraUpdate = std::uint32_t(__stdcall*)(void* camera, std::uint32_t time);

struct Vector {
    float x;
    float y;
    float z;
};

float degrees(float radians) { return radians * degrees_per_radian; }

// Appends the worm, player and weapon part of an aim trace line.
void describe_worm(const std::byte* worm, char* text, std::size_t size) {
    const auto* player = field<const std::byte*>(worm, worm_player);
    int look_raw_x = 0;
    int look_raw_y = 0;
    int look_smooth_x = 0;
    int look_smooth_y = 0;
    const auto* service = *reinterpret_cast<const std::byte* const*>(logic_control_service);
    const auto* data = player != nullptr ? field<const std::byte*>(player, player_data) : nullptr;
    if (data != nullptr && service != nullptr) {
        const auto record = static_cast<std::uintptr_t>(field<std::int32_t>(data, player_controller)) * 8;
        look_raw_x = field<std::int8_t>(service, raw_records + record + 6);
        look_raw_y = field<std::int8_t>(service, raw_records + record + 7);
        look_smooth_x = field<std::int8_t>(service, smoothed_records + record + 6);
        look_smooth_y = field<std::int8_t>(service, smoothed_records + record + 7);
    }
    char weapon_text[160] = " weapon=none";
    const auto* weapon = player != nullptr ? field<const std::byte*>(player, player_weapon) : nullptr;
    if (weapon != nullptr) {
        const auto* weapon_info = field<const std::byte*>(weapon, weapon_data);
        std::array<int, 3> flags = {-1, -1, -1};
        if (weapon_info != nullptr) {
            for (std::size_t index = 0; index < flags.size(); ++index) {
                flags[index] = field<std::uint8_t>(weapon_info, weapon_flags[index]);
            }
        }
        std::snprintf(weapon_text, sizeof weapon_text, " weapon yaw=%.3f pitch=%.3f mode=%d flags=%d/%d/%d",
                      degrees(field<float>(weapon, weapon_yaw)), degrees(field<float>(weapon, weapon_pitch)),
                      field<std::int32_t>(weapon, weapon_aim_mode), flags[0], flags[1], flags[2]);
    }
    std::snprintf(text, size,
                  " look raw=(%d,%d) smooth=(%d,%d) worm turn=%.3f pitchRate=%.3f pitch=%.3f state=%d flags=%02x%s",
                  look_raw_x, look_raw_y, look_smooth_x, look_smooth_y, degrees(field<float>(worm, worm_turn_rate)),
                  degrees(field<float>(worm, worm_pitch_rate)), degrees(field<float>(worm, worm_aim_pitch)),
                  field<std::int32_t>(worm, worm_state), field<std::uint8_t>(worm, worm_flags), weapon_text);
}

void trace_aim(const void* camera, std::uint32_t time) {
    if (aim_lines > max_lines) {
        return;
    }
    if (++aim_lines > max_lines) {
        log::debug("Aim trace: limit reached, no more lines");
        return;
    }
    const Vector position = field<Vector>(camera, camera_position);
    const Vector look_at = field<Vector>(camera, camera_look_at);
    const Vector direction = {look_at.x - position.x, look_at.y - position.y, look_at.z - position.z};
    const float pitch = std::atan2(direction.y, std::hypot(direction.x, direction.z));
    const float yaw = field<float>(camera, camera_yaw);
    float yaw_change = degrees(yaw - previous_camera_yaw);
    yaw_change -= 360.0f * std::round(yaw_change / 360.0f);
    previous_camera_yaw = yaw;

    char worm_text[384] = " target is not a worm";
    const auto* targets = field<const std::byte* const*>(camera, camera_targets);
    const std::byte* target = targets != nullptr ? *targets : nullptr;
    if (target != nullptr && field<std::int32_t>(target, entity_type) == worm_type) {
        describe_worm(target, worm_text, sizeof worm_text);
    }
    char text[768];
    std::snprintf(text, sizeof text,
                  "Aim trace f=%u s=%d t=%u counts=(%.1f,%.1f) cam yaw=%.3f change=%.3f pitch=%.3f "
                  "pos=(%.1f,%.1f,%.1f)%s",
                  step_frame, steps_in_frame, time, mouse.consumed_x_after - mouse.consumed_x_before,
                  mouse.consumed_y_after - mouse.consumed_y_before, degrees(yaw), yaw_change, degrees(pitch),
                  position.x, position.y, position.z, worm_text);
    log::debug(text);
}

// Marks that the first-person aim mode ran in this camera update. __fastcall with an unused second
// parameter matches __thiscall.
std::uint32_t __fastcall traced_first_person(void* camera, void* /*unused_edx*/, void* result) {
    first_person_ran = true;
    return reinterpret_cast<FirstPerson>(first_person_function)(camera, result);
}

std::uint32_t __stdcall traced_camera_update(void* camera, std::uint32_t time) {
    first_person_ran = false;
    const std::uint32_t result = reinterpret_cast<CameraUpdate>(camera_update)(camera, time);
    if (first_person_ran) {
        trace_aim(camera, time);
    }
    return result;
}

}  // namespace

void update() {
    if (trace_installed) {
        trace_pointer();
    }
}

void install() {
    const auto report = [](bool installed, const char* hook) {
        log::debug(std::string("Drawer trace: ") + hook + (installed ? " hooked" : " NOT hooked (unexpected code)"));
    };
    report(patch::replace_dword(logic_control_update_slot, logic_control_update,
                                reinterpret_cast<std::uintptr_t>(&traced_logic_control_update)),
           "logic control update");
    report(patch::replace_dword(drawer_update_slot, drawer_update,
                                reinterpret_cast<std::uintptr_t>(&traced_drawer_update)),
           "drawer update");
    report(patch::redirect_call(mouse_path_call, mouse_path_function, &traced_mouse_path), "mouse path");
    report(patch::redirect_call(grid_path_call, grid_path_function, &traced_grid_path), "grid path");
    constexpr std::uint8_t call_opcode = 0xe8;
    constexpr std::uintptr_t call_size = 5;
    if (*reinterpret_cast<const std::uint8_t*>(first_person_call) == call_opcode) {
        first_person_function =
            first_person_call + call_size + *reinterpret_cast<const std::int32_t*>(first_person_call + 1);
    }
    const bool aim_hooked = first_person_function != 0 &&
                            patch::replace_dword(camera_update_slot, camera_update,
                                                 reinterpret_cast<std::uintptr_t>(&traced_camera_update)) &&
                            patch::redirect_call(first_person_call, first_person_function, &traced_first_person);
    log::debug(aim_hooked ? "Aim trace: camera update and first-person aim hooked"
                          : "Aim trace: NOT fully hooked (unexpected code)");
    trace_installed = true;
}

}  // namespace ofc::input_trace
