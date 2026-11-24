// The live SD card change takes and drops the image lock (GH #555): Emulator::change_sd_card.
//
// Oracle: the design in the issue and its plan: a card is held by one jnext; a live change locks the
// card it inserts (and the writes file bound to it) BEFORE it unmounts anything, drops the old card's
// locks once the switch is made, and keeps the current card mounted when the new one is held by
// another jnext. An eject frees the slot. The holder "another jnext" is a second ImageLock in this
// process: flock and LockFileEx conflict between two open handles of one process exactly as between
// two processes (SDLOCK-02). Locking is enabled here as main() enables it (sd_locks::set_enabled).
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "core/emulator.h"
#include "core/image_lock.h"
#include "peripheral/sd_write_journal.h"
#include "../row_id.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sdcard::ImageLock;
namespace locks = sdcard::sd_locks;

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

bool write_card(const std::string& path, uint8_t fill) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    const std::vector<char> b(64 * 1024, static_cast<char>(fill));
    f.write(b.data(), static_cast<std::streamsize>(b.size()));
    return f.good();
}

bool bind_journal(const std::string& image, const std::string& file) {
    sd_write_journal::Binding b;
    std::error_code ec;
    b.bytes = fs::file_size(image, ec);
    b.mtime = static_cast<int64_t>(fs::last_write_time(image, ec).time_since_epoch().count());
    std::string why;
    return sd_write_journal::bind(image, file, b, false, why);
}

// Held by "another jnext"?
bool is_held(const std::string& path, ImageLock::Kind kind = ImageLock::Kind::Image) {
    std::string why;
    return !ImageLock::acquire(path, kind, why).has_value();
}

EmulatorConfig cfg_with(const std::string& card) {
    EmulatorConfig c;
    c.type = MachineType::ZX48K;
    c.sd_card_image = card;
    return c;
}

}  // namespace

int main() {
    const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path dir = fs::temp_directory_path() / ("jnext-swaplock-" + stamp);
    fs::create_directories(dir);
    // sd_locks puts its lock files under the state dir: a scratch one, never the developer's.
#ifdef _WIN32
    _putenv_s("JNEXT_CONFIG_DIR", (dir / "state").string().c_str());
#else
    setenv("JNEXT_CONFIG_DIR", (dir / "state").string().c_str(), 1);
#endif
    const std::string a = (dir / "a.img").string(), x = (dir / "x.img").string(), y = (dir / "y.img").string();
    write_card(a, 0xA1);
    write_card(x, 0xB2);
    write_card(y, 0xC3);
    locks::set_enabled(true);
    std::string why;

    {
        // as main() starts a session: the card is locked, then the emulator mounts it
        locks::hold(0, a, {}, why);
        Emulator emu;
        emu.init(cfg_with(a));
        const std::string r = emu.change_sd_card({x, false});
        check("SWL-01", "a live insert of a free card holds it and frees the old one: the inserted card is locked, the card taken out is not, and the slot mounts the new card",
              r.empty() && is_held(x) && !is_held(a) && emu.sd_card().image_path() == x, r);
        locks::release(0);
    }
    {
        // the card has a bound writes file, held with it
        const std::string w = (dir / "a.jsdw").string();
        bind_journal(a, w);
        locks::hold(0, a, w, why);
        Emulator emu;
        emu.init(cfg_with(a));
        const std::string r = emu.change_sd_card({a, false});   // the same card again
        check("SWL-02", "re-inserting the same card keeps its writes-file lock (and its image lock)",
              r.empty() && is_held(a) && is_held(w, ImageLock::Kind::Writes), r);
        locks::release(0);
        sd_write_journal::forget_all_for_test();
    }
    {
        locks::hold(0, a, {}, why);
        Emulator emu;
        emu.init(cfg_with(a));
        std::string ow;
        auto another_jnext = ImageLock::acquire(y, ImageLock::Kind::Image, ow);   // y is held elsewhere
        const std::string r = emu.change_sd_card({y, false});
        check("SWL-03", "a live insert of a card another jnext holds is refused BEFORE anything is unmounted: the current card stays mounted and locked, the held card is never mounted",
              !r.empty() && r.find("in use by another jnext") != std::string::npos && emu.sd_card().mounted() &&
                  emu.sd_card().image_path() == a && emu.config().sd_card_image == a && is_held(a),
              r);
        another_jnext.reset();
        locks::release(0);
    }
    {
        locks::hold(0, a, {}, why);
        Emulator emu;
        emu.init(cfg_with(a));
        const std::string r = emu.change_sd_card({"", false});   // eject
        check("SWL-04", "an eject releases the slot's locks: the card taken out is free", r.empty() && !is_held(a), r);
        locks::release(0);
    }
    {
        // swapping away from a card with a bound writes file and back takes the file again
        const std::string w = (dir / "a2.jsdw").string();
        bind_journal(a, w);
        locks::hold(0, a, w, why);
        Emulator emu;
        emu.init(cfg_with(a));
        const std::string away = emu.change_sd_card({x, false});
        const bool freed = !is_held(w, ImageLock::Kind::Writes) && !is_held(a);
        const std::string back = emu.change_sd_card({a, false});
        check("SWL-05", "swapping away from a card with a bound writes file frees the file, and swapping back takes it again (the registry re-attaches the journal)",
              away.empty() && back.empty() && freed && is_held(w, ImageLock::Kind::Writes) && is_held(a) && !is_held(x),
              away + "|" + back);
        locks::release(0);
        sd_write_journal::forget_all_for_test();
    }
    {
        // the second slot
        const std::string a2 = (dir / "s1a.img").string(), b2 = (dir / "s1b.img").string();
        write_card(a2, 0xD4);
        write_card(b2, 0xE5);
        locks::hold(0, a, {}, why);
        locks::hold(1, a2, {}, why);
        EmulatorConfig c = cfg_with(a);
        c.sd_card2_image = a2;
        Emulator emu;
        emu.init(c);
        const std::string r = emu.change_sd_card({b2, false, 1});
        check("SWL-06", "a live insert into the SECOND slot holds the new card and frees that slot's old one, and leaves the first slot's lock alone",
              r.empty() && is_held(b2) && !is_held(a2) && is_held(a), r);
        locks::release(0);
        locks::release(1);
    }

    locks::set_enabled(false);
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
