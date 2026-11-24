#include "core/sdcard_upgrade.h"

#include "core/fat32_image.h"
#include "core/log.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_provisioner.h"
#include "core/sha256.h"
#include "core/warm_start_cache.h"
#include "peripheral/sd_write_journal.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace sdcard::upgrade {

const char* const kWorkDir      = "upgrade-work";
const char* const kPreviousDir  = "previous";
const char* const kSwitchMarker = "SWITCHING";
const char* const kOverlayList  = "overlays.list";
const char* const kReportName   = "upgrade-report.txt";

int g_fail_switch_step_for_test = 0;

std::string work_dir(const std::string& dir) { return dir + "/" + kWorkDir; }
std::string previous_dir(const std::string& dir) { return dir + "/" + kPreviousDir; }

namespace {

namespace carry = sdcard::carry;

std::string card_of(const std::string& dir) { return dir + "/" + kFixedImageFileName; }
std::string raw_of(const std::string& dir) { return dir + "/" + kImageFileName; }

bool read_lines(const std::string& path, std::vector<std::string>& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string l;
    while (std::getline(f, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        if (!l.empty()) out.push_back(l);
    }
    return true;
}

bool write_lines(const std::string& path, const std::vector<std::string>& lines) {
    std::ofstream f(path, std::ios::trunc | std::ios::binary);
    if (!f) return false;
    for (const std::string& l : lines) f << l << "\n";
    f.flush();
    return static_cast<bool>(f);
}

// The identity a journal is bound to: size + mtime (the desktop rule), or the card's digest.
bool identity(const std::string& image, bool by_digest, sd_write_journal::Binding& b) {
    std::error_code ec;
    b = {};
    b.bytes = fs::file_size(image, ec);
    if (ec) return false;
    if (by_digest) {
        const std::string hex = sha256_file(image);
        return !hex.empty() && warm_start::parse_sha256_hex(hex, b.sha256);
    }
    const auto t = fs::last_write_time(image, ec);
    if (ec) return false;
    b.mtime = static_cast<int64_t>(t.time_since_epoch().count());
    return true;
}

// Bind `file` (an existing writes file) to `image` by either identity rule; the journal, or null.
std::shared_ptr<sd_write_journal::Journal> open_overlay(const std::string& image, const std::string& file,
                                                        std::string& why) {
    for (const bool digest : {false, true}) {
        sd_write_journal::Binding b;
        if (!identity(image, digest, b)) { why = "cannot stat '" + image + "'"; return nullptr; }
        std::string w;
        if (sd_write_journal::bind(image, file, b, false, w)) return sd_write_journal::for_image(image);
        why = w;
    }
    return nullptr;
}

// Does the writes file belong to `image`? Reads the file's header only: nothing is bound, registered or
// rewritten (a bind compacts a file with superseded records, and registers it as the card's write target).
bool overlay_belongs(const std::string& image, const std::string& file) {
    const sd_write_journal::FileInfo fi = sd_write_journal::peek_file(file);
    if (!fi.is_journal) return false;
    sd_write_journal::Binding b;
    if (!identity(image, fi.binding.by_digest(), b)) return false;
    return sd_write_journal::binding_matches(fi.binding, b);
}

void remove_quiet(const std::string& p) { std::error_code ec; fs::remove(p, ec); }

}  // namespace

// ---- overlays ---------------------------------------------------------------------------------

std::vector<std::string> usable_overlays(const std::string& dir, const std::string& card,
                                         const std::vector<std::string>& explicit_files) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& f) {
        std::error_code ec;
        const std::string key = fs::weakly_canonical(f, ec).string();
        if (seen.insert(ec ? f : key).second) out.push_back(f);
    };
    for (const std::string& f : explicit_files) add(f);
    std::vector<std::string> listed;
    const std::string list = dir + "/" + kOverlayList;
    const bool had_list = read_lines(list, listed);
    std::vector<std::string> keep;
    for (const std::string& f : listed) {
        std::error_code ec;
        if (!fs::is_regular_file(f, ec)) continue;               // gone: pruned
        if (!overlay_belongs(card, f)) continue;                  // bound to another image: pruned
        keep.push_back(f);
        add(f);
    }
    if (had_list && keep != listed) write_lines(list, keep);
    return out;
}

void register_overlay(const std::string& dir, const std::string& file) {
    std::vector<std::string> lines;
    read_lines(dir + "/" + kOverlayList, lines);
    if (std::find(lines.begin(), lines.end(), file) != lines.end()) return;
    lines.push_back(file);
    std::error_code ec;
    fs::create_directories(dir, ec);
    write_lines(dir + "/" + kOverlayList, lines);
}

bool file_binds_to_card(const std::string& card, const std::string& file) {
    std::error_code ec;
    return fs::is_regular_file(card, ec) && fs::is_regular_file(file, ec) && overlay_belongs(card, file);
}

bool file_belongs_to_backup(const std::string& dir, const std::string& file) {
    const std::string prev = previous_dir(dir) + "/" + kFixedImageFileName;
    std::error_code ec;
    if (!fs::is_regular_file(prev, ec) || !fs::is_regular_file(file, ec)) return false;
    return overlay_belongs(prev, file);
}

std::string overlay_problem(const std::string& card, const std::vector<std::string>& files) {
    uint32_t lba = 0;
    const bool have_lba = fat32_find_partition(card, lba);
    for (const std::string& f : files) {
        std::error_code ec;
        if (!fs::is_regular_file(f, ec)) continue;   // not made yet: nothing to carry
        const sd_write_journal::FileInfo fi = sd_write_journal::peek_file(f);
        if (!fi.is_journal) return "the writes file " + f + " is not a jnext writes file";
        if (!overlay_belongs(card, f)) return "the writes file " + f + " was made for another card";
        for (const uint32_t sec : fi.sectors)
            if (sec == 0 || (have_lba && sec == lba))
                return "the writes file " + f + " rewrites the partition table or the boot sector";
    }
    return {};
}

std::string backup_distribution(const std::string& dir) {
    std::ifstream f(previous_dir(dir) + "/" + kFixedImageFileName + kDistroSuffix);
    std::string l;
    if (f) std::getline(f, l);
    while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
    return l.empty() ? std::string(kLegacyDistribution) : l;
}

uint64_t space_needed(const std::string& card, uint64_t zip_bytes, const std::vector<std::string>& overlays) {
    std::error_code ec;
    uint64_t img = fs::file_size(card, ec);
    if (ec) img = 1ull << 30;
    uint64_t total = zip_bytes + 3 * img;
    for (const std::string& o : overlays) {
        const uint64_t s = fs::file_size(o, ec);
        if (!ec) total += 2 * s;
    }
    return total;
}

// ---- the carry-over -----------------------------------------------------------------------------

namespace {

struct LayerResult {
    std::vector<carry::Entry> items;
};

std::vector<std::string> sorted_keys(const std::vector<carry::Change>& user) {
    std::vector<std::string> keys;
    for (const auto& u : user) keys.push_back(carry::path_key(u.entry.path));
    std::sort(keys.begin(), keys.end());
    return keys;
}

void merge_report(carry::Report& into, const carry::Report& from) {
    auto app = [](std::vector<std::string>& a, const std::vector<std::string>& b) {
        for (const std::string& s : b) if (std::find(a.begin(), a.end(), s) == a.end()) a.push_back(s);
        std::sort(a.begin(), a.end());
    };
    app(into.carried, from.carried);
    app(into.overridden, from.overridden);
    app(into.deleted_back, from.deleted_back);
    app(into.removed, from.removed);
    app(into.unwritable, from.unwritable);
}

}  // namespace

bool carry_over(const Plan& plan, carry::Report& report, std::string& err) {
    std::vector<std::string> made_new;   // "<FILE>.new" files to remove on failure
    auto fail = [&](const std::string& why) {
        err = why;
        for (const std::string& f : made_new) remove_quiet(f);
        sd_write_journal::forget_image(plan.card);   // the registry held temporary bindings only
        sd_write_journal::forget_image(plan.new_card);
        return false;
    };
    if (!plan.carry) return true;

    const std::string work = work_dir(plan.dir);
    const std::string old_made = work + "/old-as-made.img";
    const std::string new_made = work + "/new-as-made.img";
    std::string e;
    if (!carry::rebuild_as_made(plan.raw, plan.card, old_made, e)) return fail("cannot rebuild the old card as it was made: " + e);
    if (!carry::copy_sparse_file(plan.new_card, new_made, e)) return fail("cannot keep the new card as made: " + e);

    // The user layer, and what the distribution did to the same paths.
    auto decide_layer = [&](const std::vector<carry::Change>& user, std::vector<carry::Entry>& items,
                            carry::Report& rep) -> bool {
        std::vector<carry::Change> dist;
        const auto keys = sorted_keys(user);
        auto vn = carry::Volume::open(0, new_made, nullptr, true, e);
        auto va = vn ? carry::Volume::open(1, old_made, nullptr, true, e) : nullptr;
        if (!vn || !va) return false;
        if (!carry::changes_at(*vn, *va, keys, dist, e)) return false;
        carry::decide(user, dist, *vn, items, rep);
        return true;
    };

    std::vector<carry::Entry> direct;
    {
        std::vector<carry::Change> user;
        {
            auto vc = carry::Volume::open(0, plan.card, nullptr, true, e);
            auto va = vc ? carry::Volume::open(1, old_made, nullptr, true, e) : nullptr;
            if (!vc || !va) return fail("cannot read the current card: " + e);
            if (!carry::compute_changes(*vc, *va, user, e)) return fail("cannot compare the current card: " + e);
        }
        carry::Report rep;
        if (!decide_layer(user, direct, rep)) return fail("cannot compare with the new card: " + e);
        merge_report(report, rep);
    }

    // Overlay layers, decided against the OLD card plus its overlay, before anything is written.
    struct Ov { std::string file; std::shared_ptr<sd_write_journal::Journal> old_j; std::vector<carry::Entry> items; };
    std::vector<Ov> ovs;
    for (const std::string& f : plan.overlays) {
        Ov ov;
        ov.file = f;
        std::string why;
        ov.old_j = open_overlay(plan.card, f, why);
        if (!ov.old_j) return fail("the writes file '" + f + "' does not belong to the current card: " + why);
        std::vector<carry::Change> uj;
        {
            auto view = carry::Volume::open(0, plan.card, ov.old_j, true, e);
            auto plain = view ? carry::Volume::open(1, plan.card, nullptr, true, e) : nullptr;
            if (!view || !plain) return fail("cannot read '" + f + "' over the current card: " + e);
            if (!carry::compute_changes(*view, *plain, uj, e)) return fail("cannot compare '" + f + "': " + e);
        }
        carry::Report rep;
        if (!decide_layer(uj, ov.items, rep)) return fail("cannot compare '" + f + "' with the new card: " + e);
        merge_report(report, rep);
        ovs.push_back(std::move(ov));
    }

    // 1. the direct layer into the new card
    {
        auto src = carry::Volume::open(1, plan.card, nullptr, true, e);
        auto dst = src ? carry::Volume::open(0, plan.new_card, nullptr, false, e) : nullptr;
        if (!src || !dst) return fail("cannot open the cards for the carry-over: " + e);
        const FileAddStatus st = carry::apply_carry(*src, direct, *dst, e);
        if (st != FileAddStatus::Ok) return fail("carrying your files over failed: " + e);
        if (!dst->sync(e)) return fail("cannot finish writing the new card: " + e);
    }

    // 2. each overlay, bound to the new card's final identity (the card is not written after this)
    for (Ov& ov : ovs) {
        const std::string out = ov.file + ".new";
        made_new.push_back(out);
        remove_quiet(out);
        sd_write_journal::Binding b;
        if (!identity(plan.new_card, false, b)) return fail("cannot stat the new card");
        std::string w;
        if (!sd_write_journal::bind(plan.new_card, out, b, true, w)) return fail("cannot start '" + out + "': " + w);
        auto jn = sd_write_journal::for_image(plan.new_card);
        auto src = carry::Volume::open(1, plan.card, ov.old_j, true, e);
        auto dst = src ? carry::Volume::open(0, plan.new_card, jn, false, e) : nullptr;
        if (!src || !dst) return fail("cannot open '" + ov.file + "' for the carry-over: " + e);
        if (!ov.items.empty()) {
            const FileAddStatus st = carry::apply_carry(*src, ov.items, *dst, e);
            if (st != FileAddStatus::Ok) return fail("carrying the files of '" + ov.file + "' over failed: " + e);
        }
        if (!dst->sync(e)) return fail("cannot finish writing '" + out + "': " + e);
        dst.reset();
        src.reset();
        if (!carry::copy_kept_file(*ov.old_j, *jn)) return fail("cannot copy the esxDOS record of '" + ov.file + "'");
    }
    sd_write_journal::forget_image(plan.card);
    sd_write_journal::forget_image(plan.new_card);

    // 3. verify: the carried paths equal their sources, nothing else moved
    {
        auto r = carry::Volume::open(0, plan.new_card, nullptr, true, e);
        auto s = r ? carry::Volume::open(1, plan.card, nullptr, true, e) : nullptr;
        if (!r || !s) return fail("cannot verify the new card: " + e);
        if (!carry::verify_carried_equal(*r, *s, direct, e)) return fail("verification failed: " + e);
    }
    {
        auto r = carry::Volume::open(0, plan.new_card, nullptr, true, e);
        auto n = r ? carry::Volume::open(1, new_made, nullptr, true, e) : nullptr;
        if (!r || !n) return fail("cannot verify the new card: " + e);
        if (!carry::verify_nothing_else_moved(*r, *n, direct, e)) return fail("verification failed: " + e);
    }
    for (const Ov& ov : ovs) {
        const std::string out = ov.file + ".new";
        sd_write_journal::Binding b;
        std::string w;
        if (!identity(plan.new_card, false, b) || !sd_write_journal::bind(plan.new_card, out, b, false, w))
            return fail("cannot read back '" + out + "': " + w);
        auto jn = sd_write_journal::for_image(plan.new_card);
        auto r = carry::Volume::open(0, plan.new_card, jn, true, e);
        auto s = r ? carry::Volume::open(1, plan.card, ov.old_j, true, e) : nullptr;
        if (!r || !s) return fail("cannot verify '" + out + "': " + e);
        if (!carry::verify_carried_equal(*r, *s, ov.items, e)) return fail("verification of '" + out + "' failed: " + e);
        r.reset();
        s.reset();
        auto r2 = carry::Volume::open(0, plan.new_card, jn, true, e);
        auto n2 = r2 ? carry::Volume::open(1, plan.new_card, nullptr, true, e) : nullptr;
        if (!r2 || !n2) return fail("cannot verify '" + out + "': " + e);
        if (!carry::verify_nothing_else_moved(*r2, *n2, ov.items, e)) return fail("verification of '" + out + "' failed: " + e);
    }
    sd_write_journal::forget_image(plan.card);
    sd_write_journal::forget_image(plan.new_card);
    std::vector<uint8_t> rom;
    if (!extract_sd_rom(plan.new_card, "/MACHINES/NEXT/48.rom", rom) || rom.empty())
        return fail("the new card has no 48K ROM after the carry-over");
    return true;
}

// ---- the switch ---------------------------------------------------------------------------------

namespace {

struct Switch {
    std::string dir;
    bool        carry = true;
    std::string distro, raw_sha;
    int         recipe = 0;
    std::vector<std::string> overlays;
};

bool parse_switch(const std::string& dir, Switch& s) {
    std::vector<std::string> lines;
    if (!read_lines(work_dir(dir) + "/" + kSwitchMarker, lines)) return false;
    s.dir = dir;
    for (const std::string& l : lines) {
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k == "carry") s.carry = v == "1";
        else if (k == "distro") s.distro = v;
        else if (k == "raw_sha") s.raw_sha = v;
        else if (k == "recipe") s.recipe = std::atoi(v.c_str());
        else if (k == "overlay") s.overlays.push_back(v);
    }
    return true;
}

// The files that make up the set, by name, with the sidecars.
std::vector<std::string> set_names() {
    const std::string c = kFixedImageFileName, r = kImageFileName;
    return {c, c + kDistroSuffix, c + kFixedImageRecipeSuffix, c + kDeclinedSuffix, r, r + ".sha256", r + kDistroSuffix};
}

bool done(const std::string& dir, int step) { return fs::exists(work_dir(dir) + "/s" + std::to_string(step) + ".done"); }
void mark(const std::string& dir, int step) { std::ofstream(work_dir(dir) + "/s" + std::to_string(step) + ".done") << "ok\n"; }

bool injected(int step, std::string& err) {
    if (g_fail_switch_step_for_test == step) {
        g_fail_switch_step_for_test = 0;
        err = "interrupted at step " + std::to_string(step) + " (test)";
        return true;
    }
    return false;
}

bool move_file(const fs::path& from, const fs::path& to, std::string& err) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) { err = "cannot move '" + from.string() + "' to '" + to.string() + "': " + ec.message(); return false; }
    return true;
}

// 1. the old set (and a copy of the overlays' bytes) into previous.new/
bool step1(const Switch& s, std::string& err) {
    const fs::path pn = fs::path(s.dir) / "previous.new";
    std::error_code ec;
    fs::create_directories(pn / "overlays", ec);
    for (const std::string& n : set_names()) {
        const fs::path src = fs::path(s.dir) / n;
        if (fs::exists(src, ec) && !fs::exists(pn / n, ec) && !move_file(src, pn / n, err)) return false;
    }
    std::vector<std::string> list;
    int i = 0;
    for (const std::string& o : s.overlays) {
        const std::string tag = std::to_string(i++) + "-" + fs::path(o).filename().string();
        fs::copy_file(o, pn / "overlays" / tag, fs::copy_options::overwrite_existing, ec);
        if (ec) { err = "cannot keep a copy of '" + o + "': " + ec.message(); return false; }
        list.push_back(tag + "\t" + o);
    }
    if (!write_lines((pn / "overlays" / "LIST").string(), list)) { err = "cannot write the overlay list"; return false; }
    return true;
}

// 2. the new raw into place, with its sidecars: a card is never in place without the raw it was made from
bool step2(const Switch& s, std::string& err) {
    const fs::path work = work_dir(s.dir);
    std::error_code ec;
    if (fs::exists(work / "raw.new", ec) && !move_file(work / "raw.new", raw_of(s.dir), err)) return false;
    const std::string raw = raw_of(s.dir);
    if (!s.raw_sha.empty() && !write_lines(raw + ".sha256", {s.raw_sha + "  " + kImageFileName})) { err = "cannot write the raw's .sha256"; return false; }
    if (!write_lines(raw + kDistroSuffix, {s.distro})) { err = "cannot write the raw's .distro"; return false; }
    return true;
}

// 3. the new card into place, with its sidecars
bool step3(const Switch& s, std::string& err) {
    const fs::path work = work_dir(s.dir);
    std::error_code ec;
    if (fs::exists(work / "card.new", ec) && !move_file(work / "card.new", card_of(s.dir), err)) return false;
    const std::string card = card_of(s.dir);
    if (!write_lines(card + kDistroSuffix, {s.distro}) || !write_lines(card + kFixedImageRecipeSuffix, {std::to_string(s.recipe)})) {
        err = "cannot write the new card's sidecars";
        return false;
    }
    return true;
}

// 4. the overlays: each "<FILE>.new" over FILE (a fresh start sets them aside instead)
bool step4(const Switch& s, std::string& err) {
    std::error_code ec;
    for (const std::string& o : s.overlays) {
        if (s.carry) {
            if (fs::exists(o + ".new", ec) && !move_file(o + ".new", o, err)) return false;
        } else {
            fs::remove(o, ec);
        }
    }
    if (!s.carry) {
        std::vector<std::string> none;
        write_lines(s.dir + "/" + kOverlayList, none);
    }
    return true;
}

// 5. any old previous/ goes, previous.new/ takes its name
bool step5(const Switch& s, std::string& err) {
    std::error_code ec;
    const fs::path prev = previous_dir(s.dir), pn = fs::path(s.dir) / "previous.new";
    if (fs::exists(pn, ec)) {
        fs::remove_all(prev, ec);
        if (!move_file(pn, prev, err)) return false;
    }
    return true;
}

// 6. the report beside the backup, the work directory gone
bool step6(const Switch& s, std::string& err) {
    std::error_code ec;
    const fs::path rep = fs::path(work_dir(s.dir)) / "report.txt";
    if (fs::exists(rep, ec)) {
        fs::create_directories(previous_dir(s.dir), ec);
        fs::copy_file(rep, fs::path(previous_dir(s.dir)) / kReportName, fs::copy_options::overwrite_existing, ec);
    }
    fs::remove_all(work_dir(s.dir), ec);
    if (ec) { err = "cannot remove the work directory: " + ec.message(); return false; }
    return true;
}

bool run_steps(const Switch& s, std::string& err) {
    using StepFn = bool (*)(const Switch&, std::string&);
    const StepFn steps[6] = {step1, step2, step3, step4, step5, step6};
    for (int k = 1; k <= 6; ++k) {
        if (k < 6 && done(s.dir, k)) continue;
        if (injected(k, err)) return false;
        if (!steps[k - 1](s, err)) return false;
        if (k < 6) mark(s.dir, k);
    }
    return true;
}

}  // namespace

bool switch_set(const Plan& plan, const std::string& report_text, std::string& err) {
    const std::string work = work_dir(plan.dir);
    {
        std::ofstream rf(work + "/report.txt", std::ios::trunc | std::ios::binary);
        rf << report_text;
        if (!rf) { err = "cannot write the report"; return false; }
    }
    std::vector<std::string> lines = {"carry=" + std::string(plan.carry ? "1" : "0"), "distro=" + plan.new_distro,
                                      "raw_sha=" + plan.new_raw_sha, "recipe=" + std::to_string(plan.new_recipe)};
    for (const std::string& o : plan.overlays) lines.push_back("overlay=" + o);
    // The marker is written to a temporary name and renamed: it either exists whole or not at all.
    if (!write_lines(work + "/SWITCHING.tmp", lines) || !move_file(work + "/SWITCHING.tmp", work + "/" + kSwitchMarker, err)) {
        if (err.empty()) err = "cannot write the switch plan";
        return false;
    }
    Switch s;
    if (!parse_switch(plan.dir, s)) { err = "cannot read the switch plan"; return false; }
    return run_steps(s, err);
}

bool recover(const std::string& dir, std::string& note, std::string& err) {
    note.clear();
    const std::string work = work_dir(dir);
    std::error_code ec;
    if (!fs::exists(work, ec)) return true;
    Switch s;
    if (fs::exists(work + "/" + kSwitchMarker, ec) && parse_switch(dir, s)) {
        note = "sdcard: finishing an interrupted upgrade";
        Log::emulator()->info("{}", note);
        return run_steps(s, err);
    }
    // Without the marker the old set was never touched: the work directory is scratch.
    fs::remove_all(work, ec);
    note = "sdcard: removed the leftovers of an interrupted upgrade";
    Log::emulator()->info("{}", note);
    return true;
}

}  // namespace sdcard::upgrade
