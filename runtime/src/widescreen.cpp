#include "widescreen.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <string>

#include "log.h"
#include "memory-patch.h"

// The game was made for 4:3 screens: its cameras, its 2D layer (a 640x480 space, x from -320 to 320
// and y from -240 to 240), its HUD layout and cutscene bars all assume that shape, and the picture
// is stretched on any other screen. We keep the vertical view and widen the horizontal one on wider
// screens (Hor+), and do the opposite on narrower ones (Vert+). Videos are not handled here: GOG's
// patch.dll replaces the game's video player and draws the frames itself.

namespace ofc::widescreen {
namespace {

constexpr double original_aspect = 4.0 / 3.0;
constexpr double virtual_half_height = 240.0;
constexpr double virtual_half_width = 320.0;

// The application object (WfApp) and its display size in pixels.
constexpr std::uintptr_t app_pointer = 0x73156c;
constexpr std::uintptr_t app_width_offset = 0x28;
constexpr std::uintptr_t app_height_offset = 0x2c;

double display_aspect() {
    const auto app = *reinterpret_cast<const std::uintptr_t*>(app_pointer);
    if (app == 0) {
        return original_aspect;
    }
    const int width = *reinterpret_cast<const int*>(app + app_width_offset);
    const int height = *reinterpret_cast<const int*>(app + app_height_offset);
    return width > 0 && height > 0 ? static_cast<double>(width) / height : original_aspect;
}

// --- Cameras and the 2D layer -------------------------------------------------------------------
// XCamera::BuildProjection (0x5e8cff, camera in ecx, no arguments) turns the frustum stored at
// +0x104 into the projection matrix. Its only callers are XCamera::SetFrustum, used by the game
// camera, the engine's default camera and the camera of the 2D layer (a perspective camera set up in
// WfApp::Init), and XCamera::SetFromSceneCamera, used by the 3D menu backgrounds.
// Both set the frustum right before the call, so adjusting it there never accumulates.
//
// BuildProjection leaves eax untouched, and SetFromSceneCamera relies on that: it uses eax after the
// call. Our replacements are therefore assembler stubs that keep every register as it was, call our
// C++ code and then jump into the original function.

const std::uintptr_t build_projection_function = 0x5e8cff;  // not constexpr: used from inline assembler
constexpr std::uintptr_t set_frustum_call = 0x5e90a9;
constexpr std::uintptr_t set_from_scene_camera_call = 0x5e9125;
constexpr std::uintptr_t frustum_offset = 0x104;
constexpr std::uintptr_t orthographic_flag_offset = 0x11c;

struct Frustum {
    float left;
    float right;
    float bottom;
    float top;
};

Frustum& frustum_of(void* camera) {
    return *reinterpret_cast<Frustum*>(static_cast<std::byte*>(camera) + frustum_offset);
}

void fit_to_display(Frustum& frustum) {
    const double aspect = display_aspect();
    if (aspect > original_aspect) {
        const auto scale = static_cast<float>(aspect / original_aspect);
        frustum.left *= scale;
        frustum.right *= scale;
    } else if (aspect < original_aspect) {
        const auto scale = static_cast<float>(original_aspect / aspect);
        frustum.bottom *= scale;
        frustum.top *= scale;
    }
}

// Debug mode: logs each distinct projection once (up to a limit), before and after our change.
void log_projection(const char* source, void* camera, const Frustum& before) {
    constexpr std::size_t max_logged = 300;
    static std::set<std::string> logged;
    if (!log::debug_enabled() || logged.size() >= max_logged) {
        return;
    }
    const Frustum& after = frustum_of(camera);
    const bool orthographic = *(static_cast<const std::byte*>(camera) + orthographic_flag_offset) != std::byte{0};
    char text[192];
    std::snprintf(text, sizeof text, "%s %s l=%.3f r=%.3f b=%.3f t=%.3f -> l=%.3f r=%.3f b=%.3f t=%.3f", source,
                  orthographic ? "orthographic" : "perspective", before.left, before.right, before.bottom, before.top,
                  after.left, after.right, after.bottom, after.top);
    if (logged.insert(text).second) {
        log::debug(std::string("Projection: ") + text);
    }
}

// The 2D layer's camera, once WfApp::Init has set it up (see "The 2D layer" below). Its frustum is
// managed by update(), so adjust_set_frustum leaves it alone.
void* two_d_camera = nullptr;

// Only frusta with the 4:3 shape of the screen are adjusted, in case SetFrustum also serves
// something other than the screen.
void __stdcall adjust_set_frustum(void* camera) {
    if (camera == two_d_camera) {
        return;
    }
    Frustum& frustum = frustum_of(camera);
    const Frustum before = frustum;
    const float height = frustum.top - frustum.bottom;
    if (height != 0.0f && std::abs(std::abs((frustum.right - frustum.left) / height) - original_aspect) < 0.001) {
        fit_to_display(frustum);
    }
    log_projection("SetFrustum", camera, before);
}

// Scene cameras always render to the screen, whatever shape their frustum has.
void __stdcall adjust_scene_camera(void* camera) {
    Frustum& frustum = frustum_of(camera);
    const Frustum before = frustum;
    fit_to_display(frustum);
    log_projection("SceneCamera", camera, before);
}

__declspec(naked) void build_projection_after_set_frustum() {
    __asm {
        push eax
        push ecx
        push edx
        push ecx  // the camera, argument of adjust_set_frustum (__stdcall pops it)
        call adjust_set_frustum
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [build_projection_function]
    }
}

__declspec(naked) void build_projection_for_scene_camera() {
    __asm {
        push eax
        push ecx
        push edx
        push ecx  // the camera, argument of adjust_scene_camera (__stdcall pops it)
        call adjust_scene_camera
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [build_projection_function]
    }
}

// --- The 2D layer -------------------------------------------------------------------------------
// HUD and menus share one camera, which WfApp::Init creates and gives its frustum exactly once:
// `mov eax, [edi]; call dword ptr [eax + 0x28]` (XCamera::SetFrustum) at 0x55ccea. We replace these
// two instructions with a call to remember_2d_camera, and update() then sets the frustum: widened in
// matches, where the HUD moves to the screen edges, and original (stretched, as before) in the menus,
// whose layout is made for 4:3.

constexpr std::uintptr_t init_2d_camera_set_frustum = 0x55ccea;
constexpr std::uint8_t init_2d_camera_set_frustum_code[] = {0x8b, 0x07, 0xff, 0x50, 0x28};
constexpr std::size_t set_frustum_vtable_index = 0x28 / 4;

// What XCamera::SetFrustum copies to +0x104: the frustum, near and far plane, orthographic flag.
struct FrustumDescription {
    Frustum frustum;
    float near_plane;
    float far_plane;
    std::uint32_t orthographic;
};

using SetFrustumFunction = std::int32_t(__stdcall*)(void* camera, const FrustumDescription* description);

FrustumDescription two_d_frustum{};

std::int32_t set_frustum(void* camera, const FrustumDescription& description) {
    const auto* vtable = *static_cast<const std::uintptr_t* const*>(camera);
    return reinterpret_cast<SetFrustumFunction>(vtable[set_frustum_vtable_index])(camera, &description);
}

std::int32_t __stdcall remember_2d_camera(void* camera, const FrustumDescription* description) {
    two_d_camera = camera;
    two_d_frustum = *description;
    log::debug("2D layer camera found");
    return set_frustum(camera, *description);
}

// FlowControlService (global pointer at 0x73157c) keeps a queue of flow states; its first entry
// (begin pointer at +0x34, end at +0x38) is the current state.
constexpr std::uintptr_t flow_control_pointer = 0x73157c;
constexpr std::uintptr_t state_queue_begin_offset = 0x34;
constexpr std::uintptr_t state_queue_end_offset = 0x38;

std::optional<int> flow_state() {
    const auto service = *reinterpret_cast<const std::uintptr_t*>(flow_control_pointer);
    if (service == 0) {
        return std::nullopt;
    }
    const auto begin = *reinterpret_cast<const std::uintptr_t*>(service + state_queue_begin_offset);
    const auto end = *reinterpret_cast<const std::uintptr_t*>(service + state_queue_end_offset);
    if (begin == 0 || begin == end) {
        return std::nullopt;
    }
    return *reinterpret_cast<const int*>(begin);
}

// Splash screens (1), movies (2), the title screen (3) and the menus (5). The others are matches
// (6, 7), the attract-mode demo match (4) and transitions (0, 8, 9).
bool is_menu_state(int state) { return state == 1 || state == 2 || state == 3 || state == 5; }

void apply_2d_layout(bool menus) {
    FrustumDescription description = two_d_frustum;
    if (!menus) {
        fit_to_display(description.frustum);
    }
    set_frustum(two_d_camera, description);
    log::debug(menus ? "2D layer: menu layout (4:3, stretched)" : "2D layer: match layout (widened)");
}

// --- HUD layout ---------------------------------------------------------------------------------
// With a wider 2D layer, HUD elements near the left and right edges move outwards by the extra
// width, so they keep their distance to the screen edge. Elements in the middle stay. Elements that
// slide in from outside the screen get their hidden position moved as well.

// Positions stored in WF.exe's data section, which we can write directly.
struct DataPosition {
    const char* name;
    std::uintptr_t address;
    float original_x;
};

constexpr DataPosition data_positions[] = {
    {"weapon indicator", 0x731164, 266.0f},
    {"wind", 0x731170, -250.0f},
    {"power bar", 0x731184, -256.0f},
    {"radar and map", 0x7311e4, -252.0f},
    // The weapons drawer slides between these two (WeaponsPanelLogicEntity, 0x490920).
    {"weapons drawer, shown", 0x72e9e0, 210.0f},
    {"weapons drawer, hidden", 0x72e9dc, 475.0f},
};

// Positions that are constants inside instructions, given by the address of the constant.
struct CodePosition {
    const char* name;
    std::uintptr_t operand;
    float original_x;
    float current_x;
};

CodePosition code_positions[] = {
    // HudTurnTimeLogicEntity: `mov dword ptr [edi], 266.0` at 0x494ac9.
    {"turn timer", 0x494acb, 266.0f, 266.0f},
    // WeaponsPanelLogicEntity, HUD.LightAnim: `mov dword ptr [esp + 0x20], 278.0` at 0x48f3de.
    {"weapons drawer light", 0x48f3e2, 278.0f, 278.0f},
};

bool hud_layout_found = false;

float with_edge_offset(float x, float offset) { return x + (x < 0.0f ? -offset : offset); }

void move_hud_edge_elements(float offset) {
    for (const DataPosition& position : data_positions) {
        const float x = with_edge_offset(position.original_x, offset);
        *reinterpret_cast<float*>(position.address) = x;
        log::debug("HUD " + std::string(position.name) + ": x " + std::to_string(position.original_x) + " -> " +
                   std::to_string(x));
    }
    for (CodePosition& position : code_positions) {
        const float x = with_edge_offset(position.original_x, offset);
        if (patch::replace_dword(position.operand, std::bit_cast<std::uint32_t>(position.current_x),
                                 std::bit_cast<std::uint32_t>(x))) {
            position.current_x = x;
            log::debug("HUD " + std::string(position.name) + ": x " + std::to_string(position.original_x) + " -> " +
                       std::to_string(x));
        } else {
            log::write("ERROR: unexpected code at the " + std::string(position.name) + " position; it was not moved");
        }
    }
}

// --- Cutscene bars ------------------------------------------------------------------------------
// EfmvBorderEntity reads the width of the letterbox bars (330 in the 640x480 space) through the
// address in this instruction operand. We point it at our own value, which covers wider screens.

constexpr std::uintptr_t cutscene_bar_width_operand = 0x4616bd;
constexpr std::uintptr_t original_cutscene_bar_width = 0x6f6f48;
constexpr float default_cutscene_bar_width = 330.0f;

float cutscene_bar_width = default_cutscene_bar_width;

void log_result(bool applied, const std::string& part) {
    log::write(applied ? "Widescreen: " + part : "ERROR: unexpected code; widescreen " + part + " was not applied");
}

}  // namespace

void install() {
    log_result(patch::redirect_call(set_frustum_call, build_projection_function, &build_projection_after_set_frustum),
               "game camera and 2D layer");
    log_result(
        patch::redirect_call(set_from_scene_camera_call, build_projection_function, &build_projection_for_scene_camera),
        "menu backgrounds");
    log_result(
        patch::replace_with_call(init_2d_camera_set_frustum, init_2d_camera_set_frustum_code, &remember_2d_camera),
        "2D layer");

    hud_layout_found = std::ranges::all_of(data_positions, [](const DataPosition& position) {
        return *reinterpret_cast<const float*>(position.address) == position.original_x;
    });
    log_result(hud_layout_found, "HUD layout");

    log_result(patch::replace_dword(cutscene_bar_width_operand, original_cutscene_bar_width,
                                    reinterpret_cast<std::uintptr_t>(&cutscene_bar_width)),
               "cutscene bars");
}

void update() {
    static double applied_aspect = 0.0;
    static std::optional<bool> applied_menus;

    const double aspect = display_aspect();
    const bool aspect_changed = aspect != applied_aspect;
    if (aspect_changed) {
        applied_aspect = aspect;
        const double scale = std::max(1.0, aspect / original_aspect);
        cutscene_bar_width = static_cast<float>(default_cutscene_bar_width * scale);
        const auto offset = static_cast<float>(virtual_half_width * (scale - 1.0));
        if (hud_layout_found) {
            move_hud_edge_elements(offset);
        }
        log::write("Widescreen: aspect ratio " + std::to_string(aspect) + ", HUD edge elements moved by " +
                   std::to_string(offset) + " (2D half-width " + std::to_string(virtual_half_height * aspect) + ")");
    }

    // During transitions without a known state, the current layout stays.
    static std::optional<int> logged_state;
    const std::optional<int> state = flow_state();
    if (state != logged_state) {
        logged_state = state;
        log::debug("Flow state: " + (state ? std::to_string(*state) : std::string("none")));
    }
    const bool menus = state ? is_menu_state(*state) : applied_menus.value_or(true);
    if (two_d_camera != nullptr && (aspect_changed || applied_menus != menus)) {
        applied_menus = menus;
        apply_2d_layout(menus);
    }
}

}  // namespace ofc::widescreen
