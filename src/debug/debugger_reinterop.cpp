// CAP-REX - the skoolkit and Ghidra exports over the backend (GH #389, E23-26;
// doc/design/SOURCE-LEVEL-DEBUGGING.md section 11.2).
//
// The formats are the pure writers' (`skoolkit_ctl`, `ghidra_symbols`); this file
// gathers their inputs through the backend's public verbs only - `symbols()`,
// `lookup()`, `source_page()`, `peek()`, `mem_coverage()`, `mem_coverage_enabled()`, `mem_coverage_spaces()` - and writes the result
// into a directory. Everything it calls is side-effect free, so an export changes
// no machine state.

#include "debug/debugger.h"

#include "debug/ghidra_symbols.h"
#include "debug/long_address.h"
#include "debug/source_file.h"
#include "debug/skoolkit_ctl.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <tuple>

namespace jnext {
namespace dbg {

namespace {

namespace fs = std::filesystem;

constexpr const char* kCtlHeader = "# jnext space ";

/// The first line of `path` starts with the jnext header.
bool has_jnext_header(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string first;
    std::getline(in, first);
    return first.rfind(kCtlHeader, 0) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// A name the Ghidra export writes: CPU.ghidra.txt, view.ghidra.txt, <TAG>.ghidra.txt for a
/// space TAG names. Any other *.ghidra.txt in the directory is the user's.
bool is_ghidra_export_name(const std::string& name) {
    const std::string suffix = ".ghidra.txt";
    if (!ends_with(name, suffix)) return false;
    const std::string stem = name.substr(0, name.size() - suffix.size());
    if (stem == "CPU" || stem == "view") return true;
    const auto sp = parse_space_tag(stem);
    return sp && sp->kind != MemSpace::Kind::Cpu && space_tag(*sp) == stem;
}

/// The stem of a control file the skoolkit export writes: WWWW-TAG (4 upper-case hex digits).
bool is_ctl_stem(const std::string& stem) {
    if (stem.size() < 6 || stem[4] != '-') return false;
    for (int i = 0; i < 4; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(stem[i])) || std::islower(static_cast<unsigned char>(stem[i])))
            return false;
    const auto sp = parse_space_tag(stem.substr(5));
    return sp && sp->kind != MemSpace::Kind::Cpu && space_tag(*sp) == stem.substr(5);
}

Result refuse(Result r, std::string* why, const std::string& text) {
    if (why) *why = text;
    return r;
}

/// Every label as the disassembly shows it: one entry per key (memory, address),
/// named by `lookup()`.
std::vector<reexport::Label> gather_labels(const SymbolTable& st) {
    std::vector<reexport::Label> labels;
    std::set<std::tuple<int, uint16_t, uint16_t>> seen;   // (kind or -1, index, addr)
    for (const auto& kv : st.symbols()) {
        const uint16_t addr = kv.first;
        const auto& space = kv.second.space;
        const auto key = std::make_tuple(space ? static_cast<int>(space->kind) : -1,
                                         static_cast<uint16_t>(space ? space->index : 0), addr);
        if (!seen.insert(key).second) continue;
        const auto name = st.lookup(space ? *space : MemSpace::cpu(), addr);
        if (!name) continue;
        labels.push_back({space, addr, *name});
    }
    return labels;
}

/// Write `files` into `dir`. `skoolkit` selects the stale-file rule (header-marked
/// `.ctl` and its `.bin`) over the Ghidra one (every `*.ghidra.txt`).
Result write_directory(const std::string& dir, const std::vector<reexport::File>& files,
                       bool skoolkit, ReReport* rep, std::string* why) {
    std::error_code ec;
    const fs::path root(dir);
    if (fs::exists(root, ec) && !fs::is_directory(root, ec))
        return refuse(Result::RefusedUnavailable, why, dir + " is not a directory");
    fs::create_directories(root, ec);
    if (ec) return refuse(Result::RefusedUnavailable, why, "cannot create " + dir + ": " + ec.message());

    std::set<std::string> names;
    for (const reexport::File& f : files) names.insert(f.name);

    // Nothing is written before every file in the way is known to be ours.
    for (const reexport::File& f : files) {
        const fs::path target = root / f.name;
        if (!fs::exists(target, ec)) continue;
        if (!fs::is_regular_file(target, ec))
            return refuse(Result::Unsupported, why, target.string() + " exists and is not a regular file");
        if (!skoolkit) continue;
        if (ends_with(f.name, ".ctl") && !has_jnext_header(target))
            return refuse(Result::Unsupported, why, target.string() +
                          " exists and is not a jnext control file (its first line is not '" +
                          kCtlHeader + "...'); it was not overwritten and nothing was written");
        if (ends_with(f.name, ".bin")) {
            const fs::path ctl = root / (f.name.substr(0, f.name.size() - 4) + ".ctl");
            if (!(fs::is_regular_file(ctl, ec) && has_jnext_header(ctl)))
                return refuse(Result::Unsupported, why, target.string() +
                              " exists and no jnext control file goes with it; it was not "
                              "overwritten and nothing was written");
        }
    }

    for (const reexport::File& f : files) {
        const fs::path target = root / f.name;
        const fs::path part = root / (f.name + ".part");
        {
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            out.write(f.content.data(), static_cast<std::streamsize>(f.content.size()));
            out.close();
            if (!out) {
                fs::remove(part, ec);
                return refuse(Result::RefusedUnavailable, why, "cannot write " + part.string());
            }
        }
        fs::rename(part, target, ec);
        if (ec) {
            std::error_code ec2;
            fs::remove(part, ec2);
            return refuse(Result::RefusedUnavailable, why,
                          "cannot write " + target.string() + ": " + ec.message());
        }
    }

    // A previous export's files this one did not write.
    std::vector<fs::path> stale;
    for (const fs::directory_entry& e : fs::directory_iterator(root, ec)) {
        const std::string name = e.path().filename().string();
        if (names.count(name) || !e.is_regular_file(ec)) continue;
        if (skoolkit ? (ends_with(name, ".ctl") && has_jnext_header(e.path())) : is_ghidra_export_name(name))
            stale.push_back(e.path());
    }
    for (const fs::path& p : stale) {
        fs::remove(p, ec);
        if (skoolkit) {
            const fs::path bin = fs::path(p).replace_extension(".bin");
            if (!names.count(bin.filename().string()) && is_ctl_stem(p.stem().string())) fs::remove(bin, ec);
        }
    }

    if (rep) rep->files = files.size();
    return Result::Ok;
}

void fill_report(ReReport* rep, const reexport::Output& out, const std::vector<std::string>& extra) {
    if (!rep) return;
    rep->labels = out.labels;
    rep->comments = out.comments;
    rep->notes = extra;
    rep->notes.insert(rep->notes.end(), out.notes.begin(), out.notes.end());
}

}  // namespace

Result Debugger::export_skoolkit(const std::string& dir, ReReport* report, std::string* why) const {
    if (report) *report = ReReport{};
    skoolkit::Input in;
    in.labels = gather_labels(symbols());
    for (const auto& kv : annotations().entries())
        if (!kv.second.comment.empty())
            in.comments.push_back({kv.first.space, kv.first.addr, kv.second.comment});
    std::vector<std::string> notes;
    if (!mem_coverage_enabled())
        notes.push_back("memory coverage is off (--coverage, Debug > Coverage): there is no code/data "
                        "map, and each window is one code run");

    // Every space with a key or a mark gets its store.
    std::vector<MemSpace> spaces = mem_coverage_spaces();
    for (const reexport::Label& l : in.labels)
        if (l.space && std::find(spaces.begin(), spaces.end(), *l.space) == spaces.end())
            spaces.push_back(*l.space);
    for (const reexport::Comment& c : in.comments)
        if (std::find(spaces.begin(), spaces.end(), c.space) == spaces.end()) spaces.push_back(c.space);
    for (const MemSpace sp : spaces) {
        const size_t n = space_size(sp.kind);
        skoolkit::Store st;
        st.space = sp;
        std::vector<uint8_t> bytes(n), flags(n);
        const auto pk = peek(sp, 0, n, bytes.data());
        const bool readable = pk.status == Result::Ok && pk.value == n;
        const auto cv = mem_coverage(sp, 0, n, flags.data());
        const bool marked = cv.status == Result::Ok && cv.value == n &&
                            std::any_of(flags.begin(), flags.end(), [](uint8_t f) { return f != 0; });
        if (readable) st.bytes = std::move(bytes);
        if (marked && readable) st.flags = std::move(flags);
        if (marked && !readable)
            notes.push_back("coverage of " + space_tag(sp) + " is not mapped: its bytes cannot be read, "
                            "so there are no instruction lengths");
        in.stores.push_back(std::move(st));
    }
    in.mapped_window = [this](MemSpace sp) -> std::optional<uint16_t> {
        const uint32_t w = space_size(sp.kind);
        for (uint32_t base = 0; base < 0x10000; base += w)
            if (source_page(static_cast<uint16_t>(base)) == sp) return static_cast<uint16_t>(base);
        return std::nullopt;
    };

    const reexport::Output out = skoolkit::build(in);
    const Result r = write_directory(dir, out.files, true, report, why);
    if (r == Result::Ok) fill_report(report, out, notes);
    return r;
}

Result Debugger::export_ghidra(const std::string& dir, ReReport* report, std::string* why) const {
    if (report) *report = ReReport{};
    const std::vector<reexport::Label> labels = gather_labels(symbols());
    std::vector<ghidra::ViewLabel> view;
    std::set<uint16_t> addrs;
    for (const auto& kv : symbols().symbols()) addrs.insert(kv.first);
    for (const uint16_t a : addrs)
        if (const auto name = lookup(a)) view.push_back({a, *name});

    const reexport::Output out = ghidra::build(labels, view);
    const Result r = write_directory(dir, out.files, false, report, why);
    if (r == Result::Ok) fill_report(report, out, {});
    return r;
}

namespace {

/// "<file>: <what>" for an import refusal.
Result refuse_file(Result r, std::string* why, const std::string& file, const std::string& what) {
    return refuse(r, why, file + ": " + what);
}

/// The name of a file without directory and `.ctl`.
std::string ctl_stem(const std::string& path) {
    return fs::path(path).stem().string();
}

}  // namespace

Result Debugger::import_skoolkit(const std::vector<std::string>& files, ReReport* report, std::string* why) {
    if (report) *report = ReReport{};
    UserAnnotations scratch = annotations();
    size_t labels = 0, comments = 0, titles = 0, descriptions = 0, other = 0, continued = 0;
    std::vector<std::string> notes;
    for (const std::string& f : files) {
        const std::string problem = source_file_problem(f);
        if (!problem.empty()) return refuse_file(Result::RefusedUnavailable, why, f, problem);
        std::ifstream in(f, std::ios::binary);
        if (!in) return refuse_file(Result::RefusedUnavailable, why, f, "cannot open the file");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        reexport::Imported imp;
        std::string e;
        if (!skoolkit::parse_ctl(text, ctl_stem(f), imp, e)) return refuse_file(Result::Unsupported, why, f, e);
        for (const reexport::ImportLabel& l : imp.labels) {
            const MemSpace sp = imp.space ? *imp.space : source_page(l.addr);
            const std::string bad = scratch.set_label(sp, l.addr, l.name);
            if (!bad.empty())
                return refuse_file(Result::Unsupported, why, f, "line " + std::to_string(l.line) + ": " + bad);
            ++labels;
        }
        for (const reexport::ImportComment& c : imp.comments) {
            const MemSpace sp = imp.space ? *imp.space : source_page(c.addr);
            const std::string bad = scratch.set_comment(sp, c.addr, c.text);
            if (!bad.empty())
                return refuse_file(Result::Unsupported, why, f, "line " + std::to_string(c.line) + ": " + bad);
            ++comments;
        }
        titles += imp.titles; descriptions += imp.descriptions; other += imp.other; continued += imp.continued;
        for (const std::string& n : imp.notes) notes.push_back(f + ": " + n);
    }
    const Result r = replace_annotations(std::move(scratch));
    if (r != Result::Ok) return refuse(r, why, "the store was not replaced");
    if (report) {
        report->files = files.size();
        report->labels = labels;
        report->comments = comments;
        report->notes = notes;
        if (titles) report->notes.push_back(std::to_string(titles) + " block title(s) not imported");
        if (descriptions) report->notes.push_back(std::to_string(descriptions) + " description(s) not imported");
        if (other) report->notes.push_back(std::to_string(other) + " other directive(s) not imported");
        if (continued) report->notes.push_back(std::to_string(continued) + " continuation line(s) of those not imported");
    }
    return Result::Ok;
}

Result Debugger::import_ghidra(const std::vector<std::string>& files, ReReport* report, std::string* why) {
    if (report) *report = ReReport{};
    UserAnnotations scratch = annotations();
    size_t labels = 0;
    std::vector<std::string> notes;
    for (const std::string& f : files) {
        const std::string problem = source_file_problem(f);
        if (!problem.empty()) return refuse_file(Result::RefusedUnavailable, why, f, problem);
        std::ifstream in(f, std::ios::binary);
        if (!in) return refuse_file(Result::RefusedUnavailable, why, f, "cannot open the file");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        reexport::Imported imp;
        std::string e;
        if (!ghidra::parse_symbols(text, imp, e)) return refuse_file(Result::Unsupported, why, f, e);
        for (const reexport::ImportLabel& l : imp.labels) {
            const MemSpace sp = l.space ? *l.space : source_page(l.addr);
            const std::string bad = scratch.set_label(sp, l.addr, l.name);
            if (!bad.empty())
                return refuse_file(Result::Unsupported, why, f, "line " + std::to_string(l.line) + ": " + bad);
            ++labels;
        }
        for (const std::string& n : imp.notes) notes.push_back(f + ": " + n);
    }
    const Result r = replace_annotations(std::move(scratch));
    if (r != Result::Ok) return refuse(r, why, "the store was not replaced");
    if (report) {
        report->files = files.size();
        report->labels = labels;
        report->notes = notes;
    }
    return Result::Ok;
}

}  // namespace dbg
}  // namespace jnext
