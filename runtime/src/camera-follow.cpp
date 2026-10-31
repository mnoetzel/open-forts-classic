#include "camera-follow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>

#include "game-loop.h"
#include "log.h"
#include "memory-patch.h"

// The camera (zCameraLogicEntity) smooths its position and its look-at point separately, once per
// 10 ms update (FUN_0058fdb0): pos = k * pos + (1 - k) * desired, the same for the look-at. When the
// player turns, the desired position moves around the worm, and the smoothed position cuts across
// that circle and trails behind it. The third-person follow mode (FUN_0059b390, also used by the
// weapon camera while aiming) calls the smoothing at 0x59b855. Modern replaces that call: first the
// camera's current offset from the look-at is rotated by the same rotation as the desired offset
// since the previous update, then the original smoothing runs. Turning therefore moves the camera
// at once, while following the worm, distance changes and the occlusion jumps stay smoothed. Larger
// rotations than a player can make in one update (the occlusion code turns the camera in 30 and
// 45 degree steps) are left to the smoothing.
//
// First-person aim (Aim held; FUN_0058fa90, called by the follow mode at 0x59b531) copies the
// camera's direction from the worm's aim orientation (worm +0x1c4, vtable +0x3c). Worm_InputStep
// (0x586eb0) builds that orientation before the worm's turn of the step is applied (0x588000), so
// the camera shows every turn one logic step late. With 100 steps per second at 60 frames, the
// mouse movement of a frame is used in its first step and shown in the next one, so every other
// frame shows no movement at all. Modern turns the first-person camera by the turn of the current
// step (worm +0x1b4) whenever the game applies that turn: the worm stands on the ground (+0x138 bit
// 0) and is not in state 1 or 2 (+0x148), which skip it.

namespace ofc::camera_follow {
namespace {

constexpr std::uintptr_t smoothing_call = 0x59b855;
constexpr std::uintptr_t smoothing_function = 0x58fdb0;
constexpr std::uintptr_t first_person_call = 0x59b531;
constexpr std::uintptr_t first_person_function = 0x58fa90;

// zCameraLogicEntity fields.
constexpr std::uintptr_t position_offset = 0x44;
constexpr std::uintptr_t desired_position_offset = 0x50;
constexpr std::uintptr_t look_at_offset = 0x74;
constexpr std::uintptr_t desired_look_at_offset = 0x80;
constexpr std::uintptr_t snap_blocked_offset = 0x158;
constexpr std::uintptr_t smoothed_offset = 0x159;
constexpr std::uintptr_t time_offset = 0x204;
constexpr std::uintptr_t last_time_offset = 0x208;
constexpr std::uintptr_t snap_request_offset = 0x216;
constexpr std::uintptr_t targets_offset = 0x3c;  // target list; the first entry is the main target
constexpr std::uintptr_t up_offset = 0xb0;
constexpr std::uintptr_t yaw_offset = 0x1e0;

// Entity and worm fields.
constexpr std::uintptr_t entity_type_offset = 0xe4;
constexpr std::int32_t worm_type = 1;
constexpr std::uintptr_t worm_turn_offset = 0x1b4;  // turn of this step (radians)
constexpr std::uintptr_t worm_flags_offset = 0x138;
constexpr std::uint8_t worm_on_ground = 0x01;
constexpr std::uintptr_t worm_state_offset = 0x148;
constexpr std::array<std::int32_t, 2> states_without_turn = {1, 2};

constexpr float max_player_rotation = 25.0f * std::numbers::pi_v<float> / 180.0f;
constexpr float min_rotation = 1e-6f;
constexpr float min_length = 1e-3f;

struct Vector {
    float x;
    float y;
    float z;
};

Vector operator+(Vector a, Vector b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector operator-(Vector a, Vector b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vector operator*(Vector a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vector a, Vector b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vector cross(Vector a, Vector b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(Vector a) { return std::sqrt(dot(a, a)); }

template <typename T>
T& field(std::byte* object, std::uintptr_t offset) {
    return *reinterpret_cast<T*>(object + offset);
}

Vector previous_desired_offset{};
bool previous_valid = false;
std::uint32_t skipped_log_frame = 0;

// Rotates `v` by the rotation that turns direction `from` into direction `to` (Rodrigues).
// Returns false (and leaves `v` alone) if there is nothing to rotate or the rotation is too large.
bool rotate_like(Vector& v, Vector from, Vector to) {
    const float from_length = length(from);
    const float to_length = length(to);
    if (from_length < min_length || to_length < min_length) {
        return false;
    }
    const Vector axis = cross(from, to) * (1.0f / (from_length * to_length));
    const float sine = length(axis);
    const float cosine = dot(from, to) / (from_length * to_length);
    const float angle = std::atan2(sine, cosine);
    if (angle < min_rotation) {
        return false;
    }
    if (angle > max_player_rotation) {
        if (game_loop::frame_number() - skipped_log_frame > 60) {
            skipped_log_frame = game_loop::frame_number();
            log::debug("Camera follow: rotation of " + std::to_string(angle * 180.0f / std::numbers::pi_v<float>) +
                       " degrees in one update left to the smoothing");
        }
        return false;
    }
    const Vector unit = axis * (1.0f / sine);
    v = v * cosine + cross(unit, v) * sine + unit * (dot(unit, v) * (1.0f - cosine));
    return true;
}

// Replaces FUN_0058fdb0 (__thiscall camera, position inertia, look-at inertia, snap; ret 0xc) at the
// follow mode's call site. __fastcall with an unused second parameter matches __thiscall. The timing
// and snap handling are the original's.
void __fastcall smooth_with_rigid_orbit(std::byte* camera, void* /*unused_edx*/, float position_inertia,
                                        float look_at_inertia, std::uint8_t snap) {
    Vector& position = field<Vector>(camera, position_offset);
    Vector& look_at = field<Vector>(camera, look_at_offset);
    const Vector desired_position = field<Vector>(camera, desired_position_offset);
    const Vector desired_look_at = field<Vector>(camera, desired_look_at_offset);
    const Vector desired_offset = desired_position - desired_look_at;

    float updates = 1.0f;
    const bool snapping = snap != 0 && field<std::uint8_t>(camera, snap_blocked_offset) == 0;
    if (snapping) {
        position_inertia = 0.0f;
        look_at_inertia = 0.0f;
        field<std::uint8_t>(camera, snap_request_offset) = 0;
    } else {
        // Elapsed milliseconds times 0.001, as in the original (one blend per update in practice).
        const std::uint32_t elapsed =
            field<std::uint32_t>(camera, time_offset) - field<std::uint32_t>(camera, last_time_offset);
        updates = static_cast<float>(elapsed) * 0.001f;
    }
    field<std::uint32_t>(camera, last_time_offset) = field<std::uint32_t>(camera, time_offset);

    if (updates > 0.0f && !snapping && previous_valid) {
        Vector offset = position - look_at;
        if (rotate_like(offset, previous_desired_offset, desired_offset)) {
            position = look_at + offset;
        }
    }
    previous_desired_offset = desired_offset;
    previous_valid = true;

    for (; updates > 0.0f; updates -= 1.0f) {
        position = position * position_inertia + desired_position * (1.0f - position_inertia);
        look_at = look_at * look_at_inertia + desired_look_at * (1.0f - look_at_inertia);
    }
    field<std::uint8_t>(camera, smoothed_offset) = 1;
}

// --- First-person aim -----------------------------------------------------------------------------

using FirstPerson = std::uint32_t(__thiscall*)(std::byte* camera, void* result);

// Turns `v` about the vertical axis so that its yaw (atan2(x, z)) grows by `angle`.
Vector turn_about_vertical(Vector v, float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {v.x * cosine + v.z * sine, v.y, v.z * cosine - v.x * sine};
}

std::uint32_t turned_log_frame = 0;

// The turn the game applies to `target` in this step, or 0.
float turn_of_this_step(std::byte* target) {
    if (target == nullptr || field<std::int32_t>(target, entity_type_offset) != worm_type ||
        (field<std::uint8_t>(target, worm_flags_offset) & worm_on_ground) == 0 ||
        std::ranges::find(states_without_turn, field<std::int32_t>(target, worm_state_offset)) !=
            states_without_turn.end()) {
        return 0.0f;
    }
    return field<float>(target, worm_turn_offset);
}

// Replaces FUN_0058fa90 at the follow mode's call site. __fastcall with an unused second parameter
// matches __thiscall.
std::uint32_t __fastcall first_person_with_current_turn(std::byte* camera, void* /*unused_edx*/, void* result) {
    const std::uint32_t outcome = reinterpret_cast<FirstPerson>(first_person_function)(camera, result);
    auto* const* targets = field<std::byte**>(camera, targets_offset);
    const float turn = turn_of_this_step(targets != nullptr ? *targets : nullptr);
    if (turn == 0.0f) {
        return outcome;
    }
    Vector& look_at = field<Vector>(camera, look_at_offset);
    const Vector position = field<Vector>(camera, position_offset);
    look_at = position + turn_about_vertical(look_at - position, turn);
    field<Vector>(camera, up_offset) = turn_about_vertical(field<Vector>(camera, up_offset), turn);
    field<float>(camera, yaw_offset) += turn;
    if (game_loop::frame_number() - turned_log_frame > 60) {
        turned_log_frame = game_loop::frame_number();
        log::debug("Camera follow: first-person camera turned by this step's " +
                   std::to_string(turn * 180.0f / std::numbers::pi_v<float>) + " degrees");
    }
    return outcome;
}

}  // namespace

void install() {
    const bool applied = patch::redirect_call(smoothing_call, smoothing_function, &smooth_with_rigid_orbit);
    log::write(applied ? "Controls: third-person camera turns without lag"
                       : "ERROR: unexpected code; camera follow was not changed");
    const bool first_person =
        patch::redirect_call(first_person_call, first_person_function, &first_person_with_current_turn);
    log::write(first_person ? "Controls: first-person camera shows the turn in the same step"
                            : "ERROR: unexpected code; first-person camera was not changed");
}

}  // namespace ofc::camera_follow
