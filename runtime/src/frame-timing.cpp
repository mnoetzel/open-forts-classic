#include "frame-timing.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "log.h"
#include "memory-patch.h"

namespace ofc::frame_timing {
namespace {

// XomTaskAppBase::Frame(unsigned dt_ms) in WF.exe (GOG build) and the vtable slots that point to it.
constexpr std::uintptr_t frame_function = 0x5e1885;
constexpr std::uintptr_t frame_vtable_slots[] = {0x6b5714, 0x6c22b4, 0x6c235c};
// Inside Frame: `call _ftol2`, which truncates the scaled frame time (dt_ms * game speed).
constexpr std::uintptr_t scaled_time_call = 0x5e18af;
constexpr std::uintptr_t msvc_ftol2 = 0x5e0d40;

// --- Game clock ---------------------------------------------------------------------------------
// The game truncates each frame's scaled time to whole milliseconds and drops the fraction, so its
// clock slows down as the frame rate rises and stops completely at 1000 fps. We carry the fraction
// over to the next frame instead.

double carried_ms = 0.0;

unsigned __stdcall whole_ms_with_carry(double scaled_ms) {
    const double total = scaled_ms + carried_ms;
    const double whole = std::floor(total);
    carried_ms = total - whole;
    return static_cast<unsigned>(whole);
}

// Stands in for the `call _ftol2` in Frame: takes the value from the x87 stack like _ftol2 does
// and returns the whole milliseconds in eax.
__declspec(naked) void ftol2_with_carry() {
    __asm {
        sub esp, 8
        fstp qword ptr [esp]
        call whole_ms_with_carry  // __stdcall: pops its 8-byte argument
        ret
    }
}

// --- Frame limiter ------------------------------------------------------------------------------
// The game renders as fast as it can. We wait after each frame until the next one is due, at the
// display's current refresh rate.

class FrameLimiter {
public:
    FrameLimiter() {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        ticks_per_second_ = frequency.QuadPart;
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (timer_ == nullptr) {  // Windows before 10 1803: a normal timer is less precise but works.
            timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
    }

    FrameLimiter(const FrameLimiter&) = delete;
    FrameLimiter& operator=(const FrameLimiter&) = delete;

    void wait_for_next_frame() {
        const long long now = ticks();
        if (now >= next_refresh_check_) {
            update_frame_period();
            next_refresh_check_ = now + ticks_per_second_;
        }
        if (frame_period_ == 0 || timer_ == nullptr) {
            return;
        }
        next_frame_ += frame_period_;
        if (next_frame_ <= now) {  // Running late: start a new schedule from now.
            next_frame_ = now;
            return;
        }
        LARGE_INTEGER due;
        due.QuadPart = -((next_frame_ - now) * 10'000'000 / ticks_per_second_);  // relative, in 100 ns
        SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(timer_, INFINITE);
    }

private:
    static long long ticks() {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }

    // The game can switch display modes, so the refresh rate is checked again every second.
    void update_frame_period() {
        DEVMODEW mode{};
        mode.dmSize = sizeof mode;
        const DWORD refresh_rate =
            EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) ? mode.dmDisplayFrequency : 0;
        if (refresh_rate == refresh_rate_) {
            return;
        }
        refresh_rate_ = refresh_rate;
        // 0 and 1 mean "hardware default", which tells us nothing: don't limit then.
        frame_period_ = refresh_rate > 1 ? ticks_per_second_ / refresh_rate : 0;
        log::write(frame_period_ != 0 ? "Frame limiter: " + std::to_string(refresh_rate) + " fps (display refresh rate)"
                                      : std::string("Frame limiter: off (display refresh rate unknown)"));
    }

    HANDLE timer_ = nullptr;
    long long ticks_per_second_ = 0;
    long long frame_period_ = 0;
    long long next_frame_ = 0;
    long long next_refresh_check_ = 0;
    DWORD refresh_rate_ = 0;
};

using FrameFunction = void(__thiscall*)(void* app, unsigned dt_ms);

// Replaces Frame in the vtables. __fastcall with an unused second parameter matches __thiscall.
void __fastcall frame_with_limiter(void* app, void* /*unused_edx*/, unsigned dt_ms) {
    reinterpret_cast<FrameFunction>(frame_function)(app, dt_ms);
    static FrameLimiter limiter;
    limiter.wait_for_next_frame();
}

}  // namespace

void install() {
    log::write(patch::redirect_call(scaled_time_call, msvc_ftol2, &ftol2_with_carry)
                   ? "Game clock: fractions of a millisecond are carried over between frames"
                   : "ERROR: unexpected code in Frame; the game clock fix was not applied");

    int replaced = 0;
    for (const std::uintptr_t slot : frame_vtable_slots) {
        replaced += patch::replace_pointer(slot, frame_function, reinterpret_cast<const void*>(&frame_with_limiter));
    }
    log::write("Frame limiter: installed in " + std::to_string(replaced) + " of " +
               std::to_string(std::size(frame_vtable_slots)) + " vtables");
}

}  // namespace ofc::frame_timing
