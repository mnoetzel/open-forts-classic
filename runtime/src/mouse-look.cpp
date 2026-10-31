#include "mouse-look.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "game-loop.h"
#include "log.h"
#include "memory-patch.h"

// How the original turns mouse movement into look input (LogicControlService::Update 0x569830):
// every logic step it takes the counts since the last step, multiplies them by 1/8, limits the
// result to +-1 and marks 8 times that as used. The value goes into a signed input byte (value * 127,
// truncated) of the controller record, which every consumer reads through four getters (0x57dec0
// .. 0x57e1d0) with a 10 % dead zone. So mouse look is capped at 8 counts per step (about 2.5 cm per
// second at 800 DPI), the rest is drained over the following steps (the view keeps moving after the
// mouse has stopped), and slow movement below the dead zone is lost.
//
// Modern keeps the byte, but gives it a coarser scale: the getters return byte / 127 * 16 for look
// axes bound to the mouse, without the dead zone, and the conversion fills the byte with exactly the
// movement of this step at the original sensitivity (above the dead zone, 7.2 counts made one axis
// unit). Every consumer (cameras, first-person and weapon aiming, the weapons drawer cursor) keeps
// its sensitivity, the cap rises to about 115 counts per step (factor 8 still lost fast flicks of 60
// counts), nothing is drained afterwards, and the fraction of a byte that is left over (one byte is
// about 0.9 counts) is carried to the next step, so the total movement stays exact.
//
// In network games the controller bytes are reduced to a few levels before they are sent, so the
// finer scale would make slow mouse movement disappear there; network games keep the original scale
// (factor 1). Every machine in a session takes the same decision, so the simulation stays in sync.
//
// Known limit: the finer scale assumes the mouse axes are bound to Look (the default). If a player
// binds them to movement instead, the movement byte gets the finer scale without the getter scaling
// it back, so the mouse moves the worm 16 times slower.

namespace ofc::mouse_look {
namespace {

constexpr float local_speed_factor = 16.0f;
constexpr float counts_per_axis_unit = 7.2f;  // 8 counts per axis unit, minus the 10 % dead zone
constexpr float byte_range = 127.0f;

// WfNetPeerService exists only during a network session (getter 0x45b2e0 reads this global).
constexpr std::uintptr_t network_peer_service = 0x7509e4;

float speed_factor() {
    return *reinterpret_cast<const std::uintptr_t*>(network_peer_service) != 0 ? 1.0f : local_speed_factor;
}

template <typename T>
T field(const std::byte* object, std::uintptr_t offset) {
    return *reinterpret_cast<const T*>(object + offset);
}

// --- Conversion of mouse counts to the input byte ------------------------------------------------
// In the conversion, the game computes the counts since the last step in st0 and multiplies them by
// 1/8 (`fmul [0x6f6fc0]`, x at 0x56997c, y at 0x569a5e); later it adds the value times 8 to the used
// counts (`fmul [0x6f6e94]`, x at 0x569a48, y at 0x569b2a, followed by `fadd [used]; fstp [used]`).
// Both multiplications become calls: the first returns our axis value, the second the full movement,
// so all counts are used and only our own carry remains.

struct MouseAxis {
    const char* name;
    float carry = 0.0f;  // fraction of a byte step left over from earlier steps
};

MouseAxis mouse_x{"x"};
MouseAxis mouse_y{"y"};
// Read by the stubs below (inline assembler cannot address struct members by name).
float movement_x = 0.0f;
float movement_y = 0.0f;

std::uint32_t capped_log_frame = 0;

// Returns the axis value whose input byte carries `movement` counts (plus the carry).
float axis_value(MouseAxis& axis, float movement) {
    const float byte_steps = movement / counts_per_axis_unit / speed_factor() * byte_range + axis.carry;
    const float byte_value = std::clamp(std::round(byte_steps), -byte_range, byte_range);
    // Beyond the cap the excess is dropped, not drained later: the view stops with the mouse.
    axis.carry = std::clamp(byte_steps - byte_value, -0.5f, 0.5f);
    if (std::abs(byte_steps) > byte_range + 0.5f && game_loop::frame_number() - capped_log_frame > 60) {
        capped_log_frame = game_loop::frame_number();
        log::debug("Mouse look: " + std::string(axis.name) + " movement of " + std::to_string(movement) +
                   " counts in one step is above the cap; the excess is dropped");
    }
    if (byte_value == 0.0f) {
        return 0.0f;
    }
    // The game stores value * 127 truncated toward zero; half a step more lands exactly on the byte.
    return (byte_value + std::copysign(0.5f, byte_value)) / byte_range;
}

float __stdcall axis_value_x(float movement) {
    movement_x = movement;
    return axis_value(mouse_x, movement);
}

float __stdcall axis_value_y(float movement) {
    movement_y = movement;
    return axis_value(mouse_y, movement);
}

// st0 holds the movement on entry and must hold the axis value on return; all registers are kept.
__declspec(naked) void convert_mouse_x() {
    __asm {
        push eax
        push ecx
        push edx
        sub esp, 4
        fstp dword ptr [esp]  // the movement becomes the argument
        call axis_value_x
        pop edx
        pop ecx
        pop eax
        ret
    }
}

__declspec(naked) void convert_mouse_y() {
    __asm {
        push eax
        push ecx
        push edx
        sub esp, 4
        fstp dword ptr [esp]
        call axis_value_y
        pop edx
        pop ecx
        pop eax
        ret
    }
}

// st0 holds the axis value on entry; returns the full movement, which the game adds to the used counts.
__declspec(naked) void used_mouse_x() {
    __asm {
        fstp st(0)
        fld dword ptr [movement_x]
        ret
    }
}

__declspec(naked) void used_mouse_y() {
    __asm {
        fstp st(0)
        fld dword ptr [movement_y]
        ret
    }
}

constexpr std::array<std::uint8_t, 6> fmul_one_eighth = {0xd8, 0x0d, 0xc0, 0x6f, 0x6f, 0x00};  // fmul [0x6f6fc0]
constexpr std::array<std::uint8_t, 6> fmul_eight = {0xd8, 0x0d, 0x94, 0x6e, 0x6f, 0x00};       // fmul [0x6f6e94]

struct ConversionSite {
    std::uintptr_t at;
    std::span<const std::uint8_t> code;
    void (*stub)();
};

const std::array conversion_sites = {
    ConversionSite{0x56997c, fmul_one_eighth, &convert_mouse_x},
    ConversionSite{0x569a48, fmul_eight, &used_mouse_x},
    ConversionSite{0x569a5e, fmul_one_eighth, &convert_mouse_y},
    ConversionSite{0x569b2a, fmul_eight, &used_mouse_y},
};

// --- Look-axis getters ------------------------------------------------------------------------------
// The four getters are __fastcall (player in ecx) and return the axis value in st0. Each starts with
// `push ecx; mov eax, [ecx + 0x98]` (7 bytes), which becomes a jump to a stub. For look axes bound to
// the mouse the stub returns byte / 127 * speed factor; otherwise it runs the two overwritten
// instructions and continues in the original getter, so keys and sticks behave exactly as before.

enum class LookAxis : std::uint8_t { x, y };

struct Getter {
    std::uintptr_t address;
    std::uintptr_t record_offset;  // controller records: 0x59 raw, 0xc2 smoothed
    LookAxis axis;
};

constexpr std::array getters = {
    Getter{0x57dec0, 0x59, LookAxis::x},
    Getter{0x57df30, 0x59, LookAxis::y},
    Getter{0x57e160, 0xc2, LookAxis::x},
    Getter{0x57e1d0, 0xc2, LookAxis::y},
};

// Where each getter continues after its first two instructions. Not constexpr: used from inline assembler.
const std::uintptr_t getter_resume[] = {0x57dec7, 0x57df37, 0x57e167, 0x57e1d7};

constexpr std::array<std::uint8_t, 7> getter_entry = {0x51, 0x8b, 0x81, 0x98, 0x00, 0x00, 0x00};

constexpr std::uintptr_t player_data_offset = 0x98;  // player entity -> PlayerDataContainer
constexpr std::uintptr_t player_index_offset = 0x84;
constexpr std::uintptr_t is_human_offset = 0x123;
constexpr std::uintptr_t logic_control_service = 0x738994;
constexpr std::uint32_t mouse_axis_binding = 0x20000000;

// The two binding slots of LookLeft/LookRight and of LookUp/LookDown in the PlayerDataContainer.
constexpr std::array<std::uintptr_t, 2> look_x_bindings = {0x98, 0x20};
constexpr std::array<std::uintptr_t, 2> look_y_bindings = {0x88, 0x64};

bool bound_to_mouse(const std::byte* player_data, LookAxis axis) {
    // CPU players drive their worms through the same bytes; they never use the finer scale.
    if (!field<bool>(player_data, is_human_offset)) {
        return false;
    }
    const auto& slots = axis == LookAxis::x ? look_x_bindings : look_y_bindings;
    return std::ranges::any_of(slots, [player_data](std::uintptr_t slot) {
        const auto binding = field<std::int32_t>(player_data, slot);
        return binding >= 0 && (static_cast<std::uint32_t>(binding) & mouse_axis_binding) != 0;
    });
}

float mouse_look_result = 0.0f;

// Returns true and sets mouse_look_result if the getter's axis is a mouse look axis.
bool __stdcall mouse_look_value(const std::byte* player, std::uint32_t getter_index) {
    const Getter& getter = getters[getter_index];
    const auto* player_data = field<const std::byte*>(player, player_data_offset);
    if (player_data == nullptr || !bound_to_mouse(player_data, getter.axis)) {
        return false;
    }
    const auto* service = *reinterpret_cast<const std::byte* const*>(logic_control_service);
    const auto controller = field<std::int32_t>(player_data, player_index_offset);
    const std::uintptr_t byte_offset = getter.axis == LookAxis::x ? 6 : 7;
    const auto value = field<std::int8_t>(service, getter.record_offset + controller * 8 + byte_offset);
    mouse_look_result = static_cast<float>(value) / byte_range * speed_factor();
    return true;
}

// On entry: [esp] = getter index, [esp + 4] = return address into the game, ecx = player.
__declspec(naked) void look_getter() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 8]  // getter index
        push ecx  // player
        call mouse_look_value
        pop edx
        pop ecx
        test al, al
        jz original_getter
        add esp, 4  // drop the getter index
        fld dword ptr [mouse_look_result]
        ret
    original_getter:
        pop eax  // getter index
        push ecx  // the getter's first instruction
        push dword ptr [getter_resume + eax * 4]
        mov eax, dword ptr [ecx + 0x98]  // its second instruction
        ret  // continue in the original getter
    }
}

__declspec(naked) void raw_look_x() {
    __asm {
        push 0
        jmp look_getter
    }
}

__declspec(naked) void raw_look_y() {
    __asm {
        push 1
        jmp look_getter
    }
}

__declspec(naked) void smoothed_look_x() {
    __asm {
        push 2
        jmp look_getter
    }
}

__declspec(naked) void smoothed_look_y() {
    __asm {
        push 3
        jmp look_getter
    }
}

const std::array<void (*)(), 4> getter_stubs = {&raw_look_x, &raw_look_y, &smoothed_look_x, &smoothed_look_y};

bool code_is(std::uintptr_t at, std::span<const std::uint8_t> expected) {
    return std::ranges::equal(std::span(reinterpret_cast<const std::uint8_t*>(at), expected.size()), expected);
}

// The conversion and the getters only work together, so either every site is changed or none.
bool all_sites_as_expected() {
    return std::ranges::all_of(conversion_sites,
                               [](const ConversionSite& site) { return code_is(site.at, site.code); }) &&
           std::ranges::all_of(getters, [](const Getter& getter) { return code_is(getter.address, getter_entry); });
}

}  // namespace

void install() {
    if (!all_sites_as_expected()) {
        log::write("ERROR: unexpected code; mouse look was not changed");
        return;
    }
    bool all = true;
    for (const ConversionSite& site : conversion_sites) {
        all = patch::replace_with_call(site.at, site.code, reinterpret_cast<const void*>(site.stub)) && all;
    }
    for (std::size_t index = 0; index < getters.size(); ++index) {
        all = patch::replace_with_jump(getters[index].address, getter_entry,
                                       reinterpret_cast<const void*>(getter_stubs[index])) &&
              all;
    }
    log::write(all ? "Controls: mouse look follows the mouse (original sensitivity, cap x" +
                         std::to_string(static_cast<int>(local_speed_factor)) + ", nothing drained after stopping)"
                   : std::string("ERROR: mouse look was only partly changed"));
}

}  // namespace ofc::mouse_look
