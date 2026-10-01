// jnext::script — the recorder. See recorder.h.

#include "script/recorder.h"

#include <cstdio>
#include <fstream>

#include "script/evaluator.h"
#include "script/key_names.h"

namespace jnext {
namespace script {

namespace {

// All inputs released: the membrane is ACTIVE-LOW, everything else
// ACTIVE-HIGH (inspect.h, InputState).
dbg::InputState idle_input() {
    dbg::InputState s;
    s.matrix.fill(0xFF);
    return s;
}

std::string hex(unsigned v, int digits) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%0*X", digits, v);
    return b;
}

std::string machine_flag(int32_t m) {
    switch (m) {
        case 0: return "48k";
        case 1: return "128k";
        case 2: return "plus3";
        default: return "next";
    }
}

std::string basename_of(const std::string& p) {
    const auto s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

}  // namespace

struct Recorder::Listen : dbg::Listener {
    Recorder& r;
    explicit Listen(Recorder& rec) : r(rec) {}
    void on_paused(const dbg::PausedInfo&) override { r.on_paused(); }
    void on_resumed(dbg::ClientId) override {}
    void on_reset(dbg::ResetKind k) override {
        if (k == dbg::ResetKind::Hard) r.on_cold_boot();
    }
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(dbg::LogLevel, const std::string&) override {}
};

Recorder::Recorder(dbg::Debugger& dbg) : dbg_(dbg) {}

Recorder::~Recorder() {
    if (recording()) {
        std::string why;
        stop(why);
    }
}

void Recorder::reset_timeline() {
    have_frame_      = false;
    first_frame_     = 0;
    last_frame_      = 0;
    last_            = idle_input();
    capture_pending_ = false;
    edges_           = 0;
    paused_mid_frame_.clear();
    lines_.clear();
    warnings_.clear();
}

bool Recorder::start(const std::string& path, std::function<RecordingInfo()> refresh, std::string& why) {
    if (recording()) {
        why = "already recording to " + path_;
        return false;
    }
    if (path.empty()) {
        why = "no file to record to";
        return false;
    }
    dbg::ClientInfo ci;
    ci.name = "recorder";
    ci.kind = dbg::ClientKind::Script;
    const auto c = dbg_.attach(ci);
    if (!c) {
        why = std::string("the recorder could not attach to the debugger (") + dbg::result_name(c.status) + ")";
        return false;
    }
    cid_ = c.value;
    listen_ = std::make_unique<Listen>(*this);
    dbg_.set_listener(cid_, listen_.get());

    // Every frame edge: sample the input, take a pending capture.
    dbg::Subscription f;
    f.kind         = dbg::EventKind::Frame;
    f.filter.frame = dbg::FRAME_EVERY;
    f.action       = dbg::Action::Continue;
    f.handler      = [this](const dbg::Event& e, dbg::Debugger&) {
        on_frame(e.frame);
        return dbg::Action::Continue;
    };
    // Host key 8 (Alt+8, or `--script-key FRAME 8`) asks for a capture.
    dbg::Subscription h;
    h.kind   = dbg::EventKind::Host;
    h.action = dbg::Action::Continue;
    std::snprintf(h.filter.host_name, sizeof h.filter.host_name, "script8");
    h.handler = [this](const dbg::Event&, dbg::Debugger&) {
        capture();
        return dbg::Action::Continue;
    };
    const auto fs = dbg_.subscribe(cid_, f);
    const auto hs = dbg_.subscribe(cid_, h);
    if (!fs || !hs) {
        dbg_.set_listener(cid_, nullptr);
        dbg_.detach(cid_);
        cid_ = dbg::CLIENT_NONE;
        why  = "the recorder could not subscribe to the frame edge";
        return false;
    }

    path_ = path;
    const auto slash = path.find_last_of('/');
    dir_  = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    base_ = basename_of(path);
    if (base_.size() > 4 && base_.compare(base_.size() - 4, 4, ".jds") == 0) base_.resize(base_.size() - 4);
    refresh_  = std::move(refresh);
    info_     = refresh_ ? refresh_() : RecordingInfo{};
    captures_ = 0;
    pngs_.clear();
    reset_timeline();
    dbg_.log(cid_, dbg::LogLevel::Info,
             "RECORD: recording to " + path_ + " from FRAME " + std::to_string(dbg_.time().frame));
    return true;
}

bool Recorder::capture() {
    if (!recording()) return false;
    capture_pending_ = true;
    return true;
}

void Recorder::warn(std::string text) {
    for (const std::string& w : warnings_)
        if (w == text) return;
    dbg_.log(cid_, dbg::LogLevel::Warn, "RECORD: WARNING: " + text);
    warnings_.push_back(std::move(text));
}

void Recorder::add(uint32_t frame, std::string text) {
    lines_.push_back(Line{frame, std::move(text)});
}

// A mid-frame pause (a breakpoint, a step) lets a key change land INSIDE a
// frame, which no `on frame` edge can reproduce (§7.2 item 4). The frame it
// paused in is remembered; a change sampled at that frame's edge is flagged.
void Recorder::on_paused() {
    if (!dbg_.at_frame_boundary()) paused_mid_frame_.insert(dbg_.time().frame);
}

// A cold boot restarts the frame numbering, and a replay starts at power-on:
// the recording starts again with the machine (and the program it loaded).
void Recorder::on_cold_boot() {
    if (refresh_) info_ = refresh_();
    const bool had = !lines_.empty();
    reset_timeline();
    dbg_.log(cid_, dbg::LogLevel::Info,
             std::string("RECORD: cold boot — the recording restarts at FRAME 0") +
                 (had ? " (what was recorded before it is dropped)" : ""));
}

void Recorder::on_frame(uint32_t k) {
    if (!have_frame_) {
        machine_ = machine_code(dbg_.machine().type);
        nr05_    = dbg_.nextreg_peek(0x05);
    } else if (k <= last_frame_) {
        warn("FRAME went back from " + std::to_string(last_frame_) + " to " + std::to_string(k) +
             " while recording (a rewind?); the replay does not go back");
    }

    const dbg::InputState st = dbg_.input_state();
    // Seen at E_k, so applied between E_k-1 and frame k: stamp k-1 (§7.2).
    const uint32_t at  = k > 0 ? k - 1 : 0;
    const std::string on = "on frame " + std::to_string(at) + " do ";
    std::size_t n = 0;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 5; ++c) {
            const bool was = !((last_.matrix[r] >> c) & 1), now = !((st.matrix[r] >> c) & 1);
            if (was == now) continue;
            add(at, on + (now ? "press" : "release") + " \"" + matrix_bit_name(r, c) + "\" end");
            ++n;
        }
    for (int id = 0; id < 16; ++id) {
        const bool was = (last_.ext_keys >> id) & 1, now = (st.ext_keys >> id) & 1;
        if (was == now) continue;
        add(at, on + (now ? "press" : "release") + " \"" + ext_key_name(id) + "\" end");
        ++n;
    }
    if (st.joy_left12 != last_.joy_left12) {
        add(at, on + "joystick 1 " + hex(st.joy_left12, 3) + " end");
        ++n;
    }
    if (st.joy_right12 != last_.joy_right12) {
        add(at, on + "joystick 2 " + hex(st.joy_right12, 3) + " end");
        ++n;
    }
    if (n) {
        edges_ += n;
        if (!have_frame_)
            warn("input already held when the recording began (FRAME " + std::to_string(k) +
                 "); the replay applies it at FRAME " + std::to_string(at));
        if (k == 0) warn("input changed before FRAME 0 ran; the replay applies it at the end of FRAME 0");
        if (paused_mid_frame_.count(k))
            warn("input change recorded while paused mid-frame at FRAME " + std::to_string(k) +
                 "; replay is not exact here");
    }
    last_ = st;
    if (!have_frame_) first_frame_ = k;
    have_frame_ = true;
    last_frame_ = k;

    if (capture_pending_) take_capture(k);
}

// At E_k, where a replay's `on frame k` rule runs — so `compare_scr` reads the
// same screen memory, and a `screenshot` is deferred to the same rendered
// frame, in both runs.
void Recorder::take_capture(uint32_t k) {
    capture_pending_ = false;
    const unsigned n = ++captures_;
    char num[16];
    std::snprintf(num, sizeof num, "%04u", n);
    const std::string name = base_ + "-" + num;
    const std::string at   = "on frame " + std::to_string(k) + " do ";

    // §7.4: the ULA screen is the byte-comparable unit; anything else on the
    // screen is compared as the composited PNG.
    const bool ula_only = !(dbg_.nextreg_peek(0x68) & 0x80) &&   // ULA enabled
                          !(dbg_.nextreg_peek(0x15) & 0x81) &&   // no LoRes, no sprites
                          !(dbg_.nextreg_peek(0x69) & 0x80) &&   // no Layer 2
                          !(dbg_.nextreg_peek(0x6B) & 0x80);     // no tilemap
    if (ula_only) {
        const std::vector<uint8_t> scr = dbg_.ula_screen_dump();
        std::ofstream out(dir_ + name + ".scr", std::ios::binary);
        out.write(reinterpret_cast<const char*>(scr.data()), static_cast<std::streamsize>(scr.size()));
        if (!out) {
            warn("capture " + std::to_string(n) + ": cannot write " + dir_ + name + ".scr");
            return;
        }
        add(k, at + "compare_scr \"" + name + ".scr\" \"capture " + std::to_string(n) + " at FRAME " +
                   std::to_string(k) + "\" end");
    } else {
        const dbg::Result r =
            dbg_.screenshot(cid_, dir_ + name + ".png", dbg::LAYER_MASK_ALL, dbg::ScreenshotFormat::Png);
        if (r != dbg::Result::Ok) {
            warn("capture " + std::to_string(n) + ": the screenshot was refused (" + dbg::result_name(r) + ")");
            return;
        }
        add(k, at + "screenshot \"" + name + "-replay.png\" end   # compare with " + name +
                   ".png (a layer other than the ULA is on, so a PNG)");
        pngs_.push_back(name);
    }
    dbg_.log(cid_, dbg::LogLevel::Info, "RECORD: capture " + std::to_string(n) + " at FRAME " + std::to_string(k));
}

std::string Recorder::script() const {
    const uint32_t first = have_frame_ ? first_frame_ : 0;
    const uint32_t exit  = (have_frame_ ? last_frame_ : 0) + 2;   // a PNG lands one rendered frame later
    std::string s;
    s += "# " + base_ + ".jds — generated by jnext (recorder v1). Record again rather than edit.\n";
    s += "# jds-recorder: 1\n";
    s += "# machine=" + machine_flag(machine_) +
         " load=" + (info_.load_file.empty() ? std::string("none") : info_.load_file) +
         " rtc=" + (info_.rtc.empty() ? std::string("live") : info_.rtc) +
         " sd=" + (info_.sd_image.empty() ? std::string("none") : basename_of(info_.sd_image)) +
         (info_.sd_id.empty() ? std::string() : " (" + info_.sd_id + ")") + "\n";
    s += "# joystick: nr05=" + hex(nr05_, 2) + "\n";
    s += "# recorded: FRAME " + std::to_string(first) + ".." + std::to_string(have_frame_ ? last_frame_ : 0) +
         ", " + std::to_string(edges_) + " input edges, " + std::to_string(captures_) + " captures\n";
    s += "# replay, in the directory holding the captures (they are named relative to it; the\n"
         "# program's path is as it was loaded):\n";
    s += "#   jnext --headless --machine " + machine_flag(machine_) +
         (info_.rtc.empty() ? std::string() : " --rtc \"" + info_.rtc + "\"") +
         (info_.load_file.empty() ? std::string() : " --load " + info_.load_file) + " --script " + base_ +
         ".jds --delayed-automatic-exit-frames " + std::to_string(exit + 50) + "\n";
    for (const std::string& w : warnings_) s += "# WARNING: " + w + "\n";
    const std::string at = "on frame " + std::to_string(first) + " once do ";
    s += at + "assert MACHINE == " + std::to_string(machine_) + " \"recorded on MACHINE " +
         std::to_string(machine_) + "\" end\n";
    s += at + "assert nextreg[0x05] == " + hex(nr05_, 2) + " \"joystick mode NR 0x05 = " + hex(nr05_, 2) +
         ", as recorded\" end\n";
    for (const std::string& w : warnings_) s += at + "log \"WARNING: " + w + "\" end\n";
    for (const Line& l : lines_) s += l.text + "\n";
    s += "on frame " + std::to_string(exit) + " do exit 0 end\n";
    return s;
}

bool Recorder::stop(std::string& why) {
    if (!recording()) {
        why = "not recording";
        return false;
    }
    if (capture_pending_) warn("a capture asked for at the end was never taken (no frame edge after it)");
    // A PNG is written one rendered frame after it was asked for (CAP-01): one
    // still pending now never will be, and its line goes with it.
    const dbg::Result fl = dbg_.flush_captures(cid_);
    if (fl == dbg::Result::NoFrame && !pngs_.empty()) {
        const std::string last = pngs_.back();
        for (auto it = lines_.begin(); it != lines_.end(); ++it)
            if (it->text.find("screenshot \"" + last + "-replay.png\"") != std::string::npos) {
                lines_.erase(it);
                break;
            }
        warn("the last PNG capture (" + last + ") was still pending when the recording stopped; dropped");
    } else if (fl == dbg::Result::RefusedUnavailable) {
        warn("a PNG capture could not be written (see the log)");
    }

    const std::string text = script();
    dbg_.set_listener(cid_, nullptr);
    dbg_.detach(cid_);
    cid_ = dbg::CLIENT_NONE;
    listen_.reset();

    std::ofstream out(path_, std::ios::binary);
    out << text;
    out.close();
    if (!out) {
        why = "cannot write " + path_;
        dbg_.log(dbg::CLIENT_NONE, dbg::LogLevel::Error, "RECORD: " + why);
        return false;
    }
    dbg_.log(dbg::CLIENT_NONE, dbg::LogLevel::Info,
             "RECORD: wrote " + path_ + " — " + std::to_string(edges_) + " input edges, " +
                 std::to_string(captures_) + " captures");
    return true;
}

}  // namespace script
}  // namespace jnext
