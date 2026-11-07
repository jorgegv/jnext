// input_integration_test row GPA-03 (GH #311), which needs a joystick SDL does not map.
//
// The row drives GamepadHost's RAW path: a virtual stick of type UNKNOWN that SDL does
// not treat as a gamepad. The Emscripten SDL3 port ships a default gamepad mapping
// (SDL_gamepad_db.h `default,*,...` under SDL_JOYSTICK_EMSCRIPTEN), so SDL_IsGamepad() is
// true for every joystick there and the raw path cannot occur. The row moved, ID, fixture
// and assertion unchanged, into a suite that is `# os: native` in test/unit-tests.conf.
// The fixture it needs (attach three pads, enumerate, assign C to Joy 1) is the one
// test_gh311_gamepad_host() builds before GPA-03, helpers shared through gpa_fixture.h.
//
// No VHDL oracle (host-side input; the FPGA only sees i_JOY_LEFT/RIGHT,
// zxnext.vhd:3441-3442).
//
// Run: ./build/test/input_integration_native_test

#include "input/joystick.h"
#include "input/joystick_dispatcher.h"
#include "input/gamepad_host.h"

#include <cstdio>
#include <string>

#include <SDL3/SDL.h>

#include "../row_id.h"
#include "gpa_fixture.h"

namespace {

int g_pass = 0, g_fail = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

void skip(const char* id, const char* reason) {
    report_row_id(id);
    ++g_skip;
    std::printf("  SKIP %s: %s\n", id, reason);
}

std::string detail(const char* f, int a, int b = 0) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), f, a, b);
    return std::string(buf);
}

}  // namespace

static void test_gh311_gpa03() {
    if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        skip("GPA-03", "SDL joystick subsystem unavailable on this host");
        return;
    }
    const SDL_JoystickID a1 = gpa_attach("jnext pad A", 0x1234, 0x0001, false);
    const SDL_JoystickID a2 = gpa_attach("jnext pad A", 0x1234, 0x0001, false);
    const SDL_JoystickID c  = gpa_attach("jnext stick C", 0x5678, 0x0002, true);
    if (!a1 || !a2 || !c) {
        skip("GPA-03", "SDL_AttachVirtualJoystick unavailable");
        SDL_Quit();
        return;
    }
    GamepadHost::set_virtual_only(true);

    const std::string H  = gpa_guid(c);

    Joystick joy; joy.reset();
    GamepadHost host(joy);
    host.enumerate_existing_devices();
    auto slot = [&](SDL_JoystickID i) { return host.dispatcher().slot_for_instance(i); };

    host.set_device(0, JoyDeviceRef{ H, "jnext stick C" });
    {
        SDL_Event b{};
        b.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
        b.jbutton.which = c; b.jbutton.button = 0; b.jbutton.down = true;
        host.handle_event(b);
        check("GPA-03", "assigning C to Joy 1 binds it live; A2 keeps Joy 2 (sticky); C's button reaches Joy 1",
              slot(c) == 0 && slot(a2) == 1 && slot(a1) == -1 &&
              (host.dispatcher().bits12(0) & 0x010) != 0,
              detail("c/a2/a1=%d", slot(c) * 100 + slot(a2) * 10 + slot(a1)));
        b.type = SDL_EVENT_JOYSTICK_BUTTON_UP; b.jbutton.down = false;
        host.handle_event(b);
    }

    SDL_DetachVirtualJoystick(a1);
    SDL_DetachVirtualJoystick(a2);
    SDL_DetachVirtualJoystick(c);
    SDL_Quit();
}

int main() {
    std::printf("input_integration_native_test (GPA-03: a joystick SDL does not map)\n");
    test_gh311_gpa03();
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
