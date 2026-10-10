#include "input/joy_assign.h"

namespace {
std::string guid_of(const std::string& id) { return id.substr(0, id.find('#')); }

int ordinal_of(const std::string& id) {
    const size_t h = id.find('#');
    return h == std::string::npos ? 1 : std::stoi(id.substr(h + 1));
}
}  // namespace

int next_joy_ordinal(const std::vector<std::string>& present_ids, const std::string& guid)
{
    for (int n = 1;; ++n) {
        bool used = false;
        for (const auto& id : present_ids) {
            if (guid_of(id) == guid && ordinal_of(id) == n) { used = true; break; }
        }
        if (!used) return n;
    }
}

std::array<int, 2> resolve_joy_assignment(const std::vector<std::string>& present_ids,
                                          const JoySource src[2],
                                          const std::string assigned[2],
                                          const int current[2])
{
    const int n = static_cast<int>(present_ids.size());
    std::array<int, 2> out{ -1, -1 };
    std::vector<bool> taken(present_ids.size(), false);
    bool done[2] = { false, false };

    for (int c = 0; c < 2; ++c) {
        if (src[c] != JoySource::Sdl) done[c] = true;           // R1
    }
    for (int c = 0; c < 2; ++c) {                               // R2
        if (done[c] || assigned[c].empty()) continue;
        for (int i = 0; i < n; ++i) {
            if (!taken[i] && present_ids[i] == assigned[c]) {
                out[c] = i; taken[i] = true; done[c] = true; break;
            }
        }
    }
    for (int c = 0; c < 2; ++c) {                               // R3
        if (done[c]) continue;
        const int cur = current[c];
        if (cur >= 0 && cur < n && !taken[cur]) {
            out[c] = cur; taken[cur] = true; done[c] = true;
        }
    }
    for (int c = 0; c < 2; ++c) {                               // R4
        if (done[c]) continue;
        for (int i = 0; i < n; ++i) {
            if (!taken[i]) { out[c] = i; taken[i] = true; break; }
        }
    }
    return out;
}

std::vector<JoyChoice> joy_choices(int /*connector*/, const std::vector<JoyDeviceInfo>& devices,
                                   JoySource src, const JoyDeviceRef& assigned)
{
    std::vector<JoyChoice> v;
    const bool sdl = (src == JoySource::Sdl);
    bool assigned_present = false;
    for (const auto& d : devices) {
        if (!assigned.id.empty() && d.id == assigned.id) assigned_present = true;
    }

    JoyChoice a;
    a.kind = JoyChoice::Kind::Auto;
    a.label = "Automatic (first free controller)";
    a.checked = sdl && assigned.id.empty();
    v.push_back(a);

    for (const auto& d : devices) {
        JoyChoice c;
        c.kind = JoyChoice::Kind::Device;
        c.id = d.id;
        c.label = d.name;
        const int ord = ordinal_of(d.id);
        if (ord >= 2) c.label += " #" + std::to_string(ord);
        c.checked = sdl && d.id == assigned.id;
        v.push_back(c);
    }
    if (!assigned.id.empty() && !assigned_present) {
        JoyChoice m;
        m.kind = JoyChoice::Kind::Missing;
        m.id = assigned.id;
        m.label = (assigned.name.empty() ? assigned.id : assigned.name) + " (not connected)";
        m.checked = sdl;
        v.push_back(m);
    }
    JoyChoice k;
    k.kind = JoyChoice::Kind::Keys;
    k.label = "Cursor Keys + Space";
    k.checked = (src == JoySource::CursorKeys);
    v.push_back(k);
    JoyChoice n;
    n.kind = JoyChoice::Kind::None;
    n.label = "None";
    n.checked = (src == JoySource::None);
    v.push_back(n);
    return v;
}
