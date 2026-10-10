#include "input/gamepad_host.h"
#include "input/joystick.h"
#include "core/log.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <sstream>

GamepadHost::~GamepadHost()
{
    for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
        close_slot(s);
    }
}

bool GamepadHost::is_raw_instance(SDL_JoystickID iid) const
{
    for (auto* js : raw_joysticks_) {
        if (js && SDL_GetJoystickID(js) == iid) return true;
    }
    return false;
}

void GamepadHost::close_slot(int s)
{
    bool had = false;
    if (controllers_[s]) {
        SDL_Joystick* js = SDL_GetGamepadJoystick(controllers_[s]);
        if (js) dispatcher_.map_instance_to_slot(SDL_GetJoystickID(js), -1);
        SDL_CloseGamepad(controllers_[s]);
        controllers_[s] = nullptr;
        had = true;
    }
    if (raw_joysticks_[s]) {
        dispatcher_.map_instance_to_slot(SDL_GetJoystickID(raw_joysticks_[s]), -1);
        SDL_CloseJoystick(raw_joysticks_[s]);
        raw_joysticks_[s] = nullptr;
        had = true;
    }
    // GH #311: with the device gone nothing will send the release for a button
    // it held, so let go of the connector here.
    if (had) dispatcher_.release_connector(s);
}

SDL_JoystickID GamepadHost::held_instance(int slot) const
{
    if (raw_joysticks_[slot]) return SDL_GetJoystickID(raw_joysticks_[slot]);
    if (controllers_[slot]) {
        SDL_Joystick* js = SDL_GetGamepadJoystick(controllers_[slot]);
        if (js) return SDL_GetJoystickID(js);
    }
    return 0;
}

int GamepadHost::add_known(SDL_JoystickID iid)
{
    // 0 is SDL3's invalid instance id (SDL_JoystickID is Uint32; SDL2 used a
    // signed id with -1).
    if (iid == 0) return -1;
    for (size_t i = 0; i < known_.size(); ++i) {
        if (known_[i].iid == iid) return -1;
    }
    if (virtual_only_ && !SDL_IsJoystickVirtual(iid)) return -1;

    const char* name = SDL_GetJoystickNameForID(iid);
    char guid[33] = {};
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(iid), guid, sizeof(guid));
    std::vector<std::string> ids;
    for (const auto& k : known_) ids.push_back(k.id);
    Known k;
    k.iid  = iid;
    k.id   = make_joy_device_id(guid, next_joy_ordinal(ids, guid));
    k.name = name ? name : "(unnamed device)";
    known_.push_back(k);
    return static_cast<int>(known_.size()) - 1;
}

void GamepadHost::open_into_slot(SDL_JoystickID iid, int slot)
{
    const char* name  = SDL_GetJoystickNameForID(iid);
    const char* label = name ? name : "(unnamed device)";
    std::string id;
    for (const auto& k : known_) if (k.iid == iid) id = k.id;

    if (SDL_IsGamepad(iid)) {
        controllers_[slot] = SDL_OpenGamepad(iid);
        if (!controllers_[slot]) {
            Log::input()->warn("Joystick '{}': SDL_OpenGamepad failed: {}",
                               label, SDL_GetError());
            return;
        }
        SDL_Joystick* js = SDL_GetGamepadJoystick(controllers_[slot]);
        if (js) dispatcher_.map_instance_to_slot(SDL_GetJoystickID(js), slot);
        Log::input()->info("Joystick {} connected: '{}' [{}] (gamepad, SDL mapping)",
                           slot + 1, label, id);
    } else {
        // No SDL gamepad mapping — open it raw. Before Task 83 this
        // device was dropped on the floor with no message at all (issue #13).
        raw_joysticks_[slot] = SDL_OpenJoystick(iid);
        if (!raw_joysticks_[slot]) {
            Log::input()->warn("Joystick '{}': SDL_OpenJoystick failed: {}",
                               label, SDL_GetError());
            return;
        }
        SDL_Joystick* js = raw_joysticks_[slot];
        dispatcher_.map_instance_to_slot(SDL_GetJoystickID(js), slot);
        Log::input()->info("Joystick {} connected: '{}' [{}] (raw joystick, no SDL "
                           "gamepad mapping: {} axes, {} buttons, {} hats)",
                           slot + 1, label, id,
                           SDL_GetNumJoystickAxes(js), SDL_GetNumJoystickButtons(js),
                           SDL_GetNumJoystickHats(js));
    }
}

void GamepadHost::reconcile()
{
    std::vector<std::string> ids;
    for (const auto& k : known_) ids.push_back(k.id);

    const int n = JoystickDispatcher::NUM_CONNECTORS;
    JoySource src[2];
    std::string asg[2];
    int cur[2] = { -1, -1 };
    for (int c = 0; c < n; ++c) {
        src[c] = dispatcher_.source(c);
        asg[c] = assigned_[c].id;
        const SDL_JoystickID held = held_instance(c);
        for (size_t i = 0; held != 0 && i < known_.size(); ++i) {
            if (known_[i].iid == held) cur[c] = static_cast<int>(i);
        }
    }
    const std::array<int, 2> want = resolve_joy_assignment(ids, src, asg, cur);

    // Close every slot whose device changes BEFORE opening any, so a device
    // moving between connectors is never open in two places at once.
    bool changed[2] = { false, false };
    for (int c = 0; c < n; ++c) {
        if (want[c] != cur[c]) {
            changed[c] = true;
            close_slot(c);
        }
    }
    for (int c = 0; c < n; ++c) {
        if (changed[c] && want[c] >= 0) open_into_slot(known_[want[c]].iid, c);
    }

    // Fallback log: once per entry into fallback, not on every re-resolve.
    for (int c = 0; c < n; ++c) {
        const bool absent = src[c] == JoySource::Sdl && !asg[c].empty() &&
                            std::find(ids.begin(), ids.end(), asg[c]) == ids.end();
        if (!absent) { fallback_logged_[c] = false; continue; }
        if (!seen_devices_ || fallback_logged_[c]) continue;
        fallback_logged_[c] = true;
        Log::input()->info("Joy {}: assigned controller '{}' [{}] is not connected; "
                           "using the first free controller instead",
                           c + 1,
                           assigned_[c].name.empty() ? std::string("(unknown name)")
                                                     : assigned_[c].name,
                           asg[c]);
    }
}

void GamepadHost::log_unbound(const std::vector<SDL_JoystickID>& arrived) const
{
    for (SDL_JoystickID iid : arrived) {
        for (const auto& k : known_) {
            if (k.iid != iid) continue;
            if (held_instance(0) == iid || held_instance(1) == iid) break;
            // Both Next pad headers are taken (or assigned to another source). Say so
            // — a silently ignored pad is exactly the complaint in issue #13.
            Log::input()->info("Joystick '{}' [{}] detected but not connected: "
                               "no free Next joystick connector", k.name, k.id);
        }
    }
}

std::vector<JoyDeviceInfo> GamepadHost::devices() const
{
    std::vector<JoyDeviceInfo> out;
    for (const auto& k : known_) {
        JoyDeviceInfo d;
        d.id   = k.id;
        d.name = k.name;
        d.connector = dispatcher_.slot_for_instance(k.iid);
        out.push_back(d);
    }
    return out;
}

void GamepadHost::set_device(int slot, const JoyDeviceRef& ref)
{
    if (slot < 0 || slot >= JoystickDispatcher::NUM_CONNECTORS) return;
    if (assigned_[slot] == ref) return;
    assigned_[slot] = ref;
    fallback_logged_[slot] = false;   // a new assignment may enter fallback afresh
    reconcile();
}

void GamepadHost::set_source(int slot, JoySource src)
{
    dispatcher_.set_source(slot, src);
    reconcile();
}

void GamepadHost::enumerate_existing_devices()
{
    // SDL3 returns a malloc'd, 0-terminated array of INSTANCE IDS; SDL2 gave
    // a count to index-loop over. The array must be released with SDL_free
    // even when the count is zero (SDL still allocates the terminator).
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    Log::input()->info("Joystick scan: {} device(s) present", count);
    if (!ids) return;
    seen_devices_ = true;
    std::vector<SDL_JoystickID> arrived;
    for (int i = 0; i < count; ++i) {
        if (add_known(ids[i]) >= 0) arrived.push_back(ids[i]);
    }
    SDL_free(ids);
    reconcile();
    log_unbound(arrived);
    if (on_devices_changed) on_devices_changed();
}

void GamepadHost::handle_event(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        // Deliberately a no-op: SDL emits BOTH this and SDL_EVENT_JOYSTICK_ADDED
        // for a gamepad-mapped device. Routing every arrival through the JOY
        // case keeps open/dedupe logic in one place and means a device with no
        // gamepad mapping takes the identical path.
        break;

    case SDL_EVENT_JOYSTICK_ADDED:
        // e.jdevice.which is the INSTANCE ID in SDL3. (Under SDL2 this one
        // event was the odd one out and carried a device INDEX instead, which
        // is why the old open_device took an index.)
        seen_devices_ = true;
        if (add_known(e.jdevice.which) >= 0) {
            reconcile();
            log_unbound({ e.jdevice.which });
            if (on_devices_changed) on_devices_changed();
        }
        break;

    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_JOYSTICK_REMOVED: {
        // e.*device.which is the instance-id here. Close whichever slot owns
        // it; the paired event then finds nothing and no-ops.
        const SDL_JoystickID iid = e.jdevice.which;
        for (size_t i = 0; i < known_.size(); ++i) {
            if (known_[i].iid != iid) continue;
            for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
                if (held_instance(s) == iid) Log::input()->info("Joystick {} disconnected", s + 1);
            }
            // The device is gone from SDL: drop it from the list, then close its
            // slot (reconcile sees the slot's device missing) and re-resolve.
            for (int s = 0; s < JoystickDispatcher::NUM_CONNECTORS; ++s) {
                if (held_instance(s) == iid) close_slot(s);
            }
            known_.erase(known_.begin() + static_cast<long>(i));
            reconcile();
            if (on_devices_changed) on_devices_changed();
            break;
        }
        break;
    }

    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_HAT_MOTION: {
        // SDL emits the JOY* family for gamepads TOO, on top of the
        // GAMEPAD* events. Applying both would register every press twice
        // (and through two different button tables), so only devices we opened
        // raw are allowed down this path.
        SDL_JoystickID iid = 0;
        switch (e.type) {
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:   iid = e.jbutton.which; break;
        case SDL_EVENT_JOYSTICK_AXIS_MOTION: iid = e.jaxis.which;   break;
        default:                             iid = e.jhat.which;    break;
        }
        if (!is_raw_instance(iid)) break;
        dispatcher_.handle_sdl_event(e);
        break;
    }

    default:
        dispatcher_.handle_sdl_event(e);
        break;
    }
}

int GamepadHost::attach_test_devices_from_env()
{
    const char* spec = std::getenv("JNEXT_TEST_VIRTUAL_JOYSTICKS");
    if (!spec || !*spec) return 0;
    virtual_only_ = true;

    int attached = 0;
    std::stringstream entries(spec);
    std::string entry;
    while (std::getline(entries, entry, ';')) {
        std::vector<std::string> f;
        std::stringstream fs(entry);
        std::string tok;
        while (std::getline(fs, tok, ':')) f.push_back(tok);
        bool raw = false;
        if (!f.empty() && f.back() == "raw") { raw = true; f.pop_back(); }
        if (f.size() < 3) {
            Log::input()->warn("JNEXT_TEST_VIRTUAL_JOYSTICKS: bad entry '{}'", entry);
            continue;
        }
        const unsigned pid = static_cast<unsigned>(std::strtoul(f.back().c_str(), nullptr, 16));
        f.pop_back();
        const unsigned vid = static_cast<unsigned>(std::strtoul(f.back().c_str(), nullptr, 16));
        f.pop_back();
        std::string name = f[0];
        for (size_t i = 1; i < f.size(); ++i) name += ":" + f[i];

        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.vendor_id  = static_cast<Uint16>(vid);
        desc.product_id = static_cast<Uint16>(pid);
        desc.name       = name.c_str();
        if (raw) {
            desc.type     = SDL_JOYSTICK_TYPE_UNKNOWN;
            desc.naxes    = 2;
            desc.nbuttons = 2;
        } else {
            desc.type     = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes    = 6;
            desc.nbuttons = 11;
            desc.nhats    = 1;
            desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_EAST) |
                               (1u << SDL_GAMEPAD_BUTTON_WEST)  | (1u << SDL_GAMEPAD_BUTTON_NORTH) |
                               (1u << SDL_GAMEPAD_BUTTON_DPAD_UP)   | (1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
                               (1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT) | (1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY);
        }
        if (SDL_AttachVirtualJoystick(&desc) == 0)
            Log::input()->warn("JNEXT_TEST_VIRTUAL_JOYSTICKS: attach '{}' failed: {}", name, SDL_GetError());
        else
            ++attached;
    }
    return attached;
}
