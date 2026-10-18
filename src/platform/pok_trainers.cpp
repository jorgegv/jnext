// POK trainer files (GH #381): the parser, and the session that applies and
// removes trainers through the debugger backend. See pok_trainers.h for the
// format, where a poke lands, and how undo works.

#include "platform/pok_trainers.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>
#include <utility>

#include "core/log.h"

namespace jnext {
namespace pok {

namespace {

using jnext::dbg::MemSpace;
using jnext::dbg::Result;

/// The largest file taken for a POK file. The biggest of 8710 real ones is
/// 7122 bytes, so this only stops a wrong file picked by mistake.
constexpr size_t kMaxPokBytes = 1024 * 1024;

bool is_blank(char c) { return c == ' ' || c == '\t'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::string hex2(unsigned v) {
    static const char digits[] = "0123456789ABCDEF";
    std::string s;
    s += digits[(v >> 4) & 15];
    s += digits[v & 15];
    return s;
}

/// "line N: what"
std::string at(int line, const std::string& what) {
    return "line " + std::to_string(line) + ": " + what;
}

/// A number as written, cut so a megabyte of digits cannot fill a message.
std::string shown(std::string_view tok) {
    return tok.size() > 10 ? std::string(tok.substr(0, 10)) + "..." : std::string(tok);
}

/// The value of a run of digits, saturating: anything above 1000000 is out of
/// every range the format has, and need not be told apart.
int to_num(std::string_view tok) {
    long v = 0;
    for (char c : tok) {
        v = v * 10 + (c - '0');
        if (v > 1000000) v = 1000000;
    }
    return static_cast<int>(v);
}

/// `s` with every byte outside 0x20-0x7E spelled \xHH: file data in a log line
/// would reach the console as raw bytes in whatever encoding the file used.
std::string ascii(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        if (c >= 0x20 && c <= 0x7E) out += static_cast<char>(c);
        else out += "\\x" + hex2(c);
    }
    return out;
}

/// "unexpected line (starts with 'X')", or "0xHH" for a byte that is not a
/// printable ASCII character (a UTF-8 byte-order mark starts with 0xEF).
std::string unexpected(unsigned char c) {
    std::string s = "unexpected line (starts with ";
    if (c >= 0x20 && c <= 0x7E) s += std::string("'") + static_cast<char>(c) + "'";
    else s += "0x" + hex2(c);
    return s + ")";
}

/// The four numbers of an `M` / `Z` line, `rest` being the line after its tag:
/// zero or more blanks, then four unsigned decimal numbers separated by one or
/// more blanks, then optional blanks. "" and `p` filled in, or the problem text.
std::string parse_poke(std::string_view rest, Poke& p) {
    static const std::string bad = "expected 4 numbers (bank address value original)";
    std::string_view tok[4];
    size_t i = 0;
    auto skip = [&]() { while (i < rest.size() && is_blank(rest[i])) ++i; };
    skip();
    for (int k = 0; k < 4; ++k) {
        if (k > 0) {
            const size_t before = i;
            skip();
            if (i == before) return bad;
        }
        const size_t start = i;
        while (i < rest.size() && is_digit(rest[i])) ++i;
        if (i == start) return bad;
        tok[k] = rest.substr(start, i - start);
    }
    skip();
    if (i != rest.size()) return bad;

    p.bank     = to_num(tok[0]);
    p.address  = to_num(tok[1]);
    p.value    = to_num(tok[2]);
    p.original = to_num(tok[3]);
    if (p.bank > 15)                         return "bank " + shown(tok[0]) + " is not 0-15";
    if (p.address < 16384 || p.address > 65535)
                                             return "address " + shown(tok[1]) + " is outside 16384-65535";
    if (p.value > 256)                       return "value " + shown(tok[2]) + " is not 0-256";
    if (p.original > 255)                    return "original " + shown(tok[3]) + " is not 0-255";
    return {};
}

}  // namespace

bool Trainer::asks_value() const {
    for (const Poke& p : pokes)
        if (p.value == 256) return true;
    return false;
}

PokFile parse_pok(std::string_view text) {
    PokFile out;
    Trainer cur;            // the trainer being read; meaningful while `open`
    bool open = false;      // an N has been seen and its Z has not
    int last_line = 0;      // the last line that belongs to `cur`

    auto fail = [&](int line, const std::string& what) {
        if (cur.problem.empty()) cur.problem = at(line, what);
    };
    // `cur` ends without its Z: a new N, the Y or the end of the file came first.
    auto end_open = [&]() {
        if (cur.problem.empty())
            cur.problem = at(last_line, cur.pokes.empty() ? "trainer has no poke lines"
                                                          : "trainer ends without its 'Z' line");
        out.trainers.push_back(std::move(cur));
        cur = Trainer{};
        open = false;
    };

    int lineno = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        // One line: up to a CR, LF or CRLF.
        size_t end = pos;
        while (end < text.size() && text[end] != '\n' && text[end] != '\r') ++end;
        const std::string_view line = text.substr(pos, end - pos);
        if (end < text.size()) {
            if (text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n') ++end;
            ++end;
        }
        pos = end;
        ++lineno;

        if (line.find_first_not_of(" \t") == std::string_view::npos) continue;   // blank

        const char tag = line[0];
        if (tag == 'N') {
            if (open) end_open();
            cur = Trainer{};
            std::string_view name = line.substr(1);
            while (!name.empty() && is_blank(name.back())) name.remove_suffix(1);
            cur.name.assign(name);
            cur.line = lineno;
            open = true;
            last_line = lineno;
        } else if (tag == 'M' || tag == 'Z') {
            if (!open) {
                out.problems.push_back(at(lineno, "poke line outside a trainer"));
                continue;
            }
            last_line = lineno;
            Poke p;
            p.line = lineno;
            const std::string why = parse_poke(line.substr(1), p);
            if (!why.empty()) fail(lineno, why);
            else cur.pokes.push_back(p);
            if (tag == 'Z') {
                out.trainers.push_back(std::move(cur));
                cur = Trainer{};
                open = false;
            }
        } else if (tag == 'Y') {
            break;
        } else if (open) {
            fail(lineno, unexpected(static_cast<unsigned char>(tag)));
        } else {
            out.problems.push_back(at(lineno, unexpected(static_cast<unsigned char>(tag))));
        }
    }
    if (open) end_open();
    return out;
}

bool load_pok_file(const std::string& path, PokFile& out, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        error = "cannot read " + path;
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    f.seekg(0, std::ios::end);
    const std::streamoff size = f ? static_cast<std::streamoff>(f.tellg()) : -1;
    if (size < 0) {
        error = "cannot read " + path;
        return false;
    }
    if (static_cast<unsigned long long>(size) > kMaxPokBytes) {
        error = path + " is larger than 1 MiB - not a POK file";
        return false;
    }
    std::string text(static_cast<size_t>(size), '\0');
    f.seekg(0);
    f.read(text.data(), size);
    if (f.gcount() != size) {
        error = "cannot read " + path;
        return false;
    }
    out = parse_pok(text);
    if (out.trainers.empty()) {
        error = path + " holds no trainers";
        if (!out.problems.empty()) error += " (" + out.problems.front() + ")";
        return false;
    }
    return true;
}

bool is_pok_path(const std::string& path) {
    static const std::string ext = ".pok";
    if (path.size() < ext.size()) return false;
    for (size_t i = 0; i < ext.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(path[path.size() - ext.size() + i])) != ext[i])
            return false;
    return true;
}

bool is_valid_utf8(std::string_view s) {
    size_t i = 0;
    auto cont = [&](size_t at_i, unsigned lo = 0x80, unsigned hi = 0xBF) {
        return at_i < s.size() &&
               static_cast<unsigned char>(s[at_i]) >= lo && static_cast<unsigned char>(s[at_i]) <= hi;
    };
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            i += 1;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (!cont(i + 1)) return false;
            i += 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            // E0 starts above the overlong forms, ED stops below the surrogates.
            const unsigned lo = c == 0xE0 ? 0xA0 : 0x80;
            const unsigned hi = c == 0xED ? 0x9F : 0xBF;
            if (!cont(i + 1, lo, hi) || !cont(i + 2)) return false;
            i += 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            const unsigned lo = c == 0xF0 ? 0x90 : 0x80;
            const unsigned hi = c == 0xF4 ? 0x8F : 0xBF;
            if (!cont(i + 1, lo, hi) || !cont(i + 2) || !cont(i + 3)) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

Session::Session(jnext::dbg::Debugger& dbg, PokFile file, std::string path)
    : dbg_(dbg), file_(std::move(file)), path_(std::move(path)) {
    records_.resize(file_.trainers.size());
}

bool Session::locate(const Poke& p, const Slots& slots, bool is_48k, Loc& loc,
                     std::string& why) const {
    const std::string where = "line " + std::to_string(p.line) + ": ";
    if (p.banked() && p.address >= 0xC000) {
        // Only the 0xC000 window is banked (see the header): the bank names the
        // pair of 8K pages, whatever the map shows there now.
        if (is_48k && p.bank != 0) {
            why = where + "bank " + std::to_string(p.bank) +
                  " is not available on a 48K (only bank 0 is at 0xC000)";
            return false;
        }
        loc.page = static_cast<uint16_t>(2 * p.bank + ((p.address >> 13) & 1));
        loc.off  = static_cast<uint16_t>(p.address & 0x1FFF);
    } else {
        const jnext::dbg::SlotInfo& s = slots[static_cast<size_t>(p.address >> 13)];
        if (s.is_rom) {
            why = where + "address " + std::to_string(p.address) + " is ROM in the current memory map";
            return false;
        }
        loc.page = s.space.index;
        loc.off  = static_cast<uint16_t>((p.address & 0x1FFF) + s.space_offset);
    }
    uint8_t b = 0;
    const auto r = dbg_.peek(MemSpace::page(loc.page), loc.off, 1, &b);
    if (r.status != Result::Ok || r.value != 1) {
        why = where + "address " + std::to_string(p.address) +
              " is not RAM in the current memory map (page 0x" + hex2(loc.page) + ")";
        return false;
    }
    loc.byte = b;
    return true;
}

bool Session::resolve(const Trainer& t, Plan& plan, std::string& why) const {
    const Slots slots = dbg_.mmu_slots();
    const bool is_48k = dbg_.machine().type == MachineType::ZX48K;
    plan = Plan{};
    std::map<uint32_t, size_t> index;   // (page, offset) -> position in plan.locs
    for (size_t k = 0; k < t.pokes.size(); ++k) {
        Loc loc;
        if (!locate(t.pokes[k], slots, is_48k, loc, why)) return false;
        const uint32_t key = (static_cast<uint32_t>(loc.page) << 16) | loc.off;
        const auto it = index.try_emplace(key, plan.locs.size());
        if (it.second) {
            plan.locs.push_back(loc);
            plan.first_poke.push_back(k);
        }
        plan.poke_loc.push_back(it.first->second);
    }
    return true;
}

std::vector<uint8_t> Session::finals(const Trainer& t, const Plan& plan,
                                     std::optional<uint8_t> value) const {
    std::vector<uint8_t> out(plan.locs.size(), 0);
    for (size_t k = 0; k < t.pokes.size(); ++k) {
        const int v = t.pokes[k].value;
        out[plan.poke_loc[k]] = v == 256 ? value.value_or(0) : static_cast<uint8_t>(v);
    }
    return out;
}

bool Session::holds(const Loc& loc, uint8_t want) const {
    uint8_t b = 0;
    const auto r = dbg_.peek(MemSpace::page(loc.page), loc.off, 1, &b);
    return r.status == Result::Ok && r.value == 1 && b == want;
}

std::string Session::poke_byte(const Loc& loc, uint8_t v) {
    const auto r = dbg_.poke(jnext::dbg::CLIENT_NONE, MemSpace::page(loc.page), loc.off, 1, &v);
    if (r.status == Result::Ok && r.value == 1) return {};
    if (r.status == Result::RefusedRzx)
        return "the machine is recording or playing back an RZX file; "
               "changing its memory would break it";
    return std::string("the debugger refused the write (") + jnext::dbg::result_name(r.status) + ")";
}

void Session::log_done(const char* verb, const Trainer& t) const {
    Log::platform()->info("POK: {} '{}' ({} poke(s)) from {}", verb, ascii(t.name),
                          t.pokes.size(), ascii(path_));
}

State Session::state(size_t i) const {
    if (i >= file_.trainers.size()) return State::NotApplied;
    const Trainer& t = file_.trainers[i];
    if (!t.problem.empty()) return State::NotApplied;

    const std::vector<Rec>& recs = records_[i];
    if (!recs.empty()) {
        bool all = true;
        for (const Rec& r : recs)
            if (!holds(r.loc, r.final_)) { all = false; break; }
        if (all) return State::Applied;
    }
    if (!t.asks_value()) {
        Plan plan;
        std::string why;
        if (resolve(t, plan, why)) {
            const std::vector<uint8_t> fin = finals(t, plan, std::nullopt);
            bool all = true;
            for (size_t j = 0; j < plan.locs.size(); ++j)
                if (plan.locs[j].byte != fin[j]) { all = false; break; }
            if (all) return State::AppliedFound;
        }
    }
    return State::NotApplied;
}

std::string Session::apply(size_t i, std::optional<uint8_t> value) {
    if (i >= file_.trainers.size()) return "no such trainer";
    const Trainer& t = file_.trainers[i];
    if (!t.problem.empty()) return t.problem;
    if (state(i) != State::NotApplied) return "already applied";

    Plan plan;
    std::string why;
    if (!resolve(t, plan, why)) return why;
    if (t.asks_value() && !value) return "this trainer asks for a value (0-255)";

    // Every original was read by resolve(), before the first byte is written.
    const std::vector<uint8_t> fin = finals(t, plan, value);
    std::vector<Rec> recs;
    for (size_t j = 0; j < plan.locs.size(); ++j) {
        Rec rec{plan.locs[j], plan.locs[j].byte, fin[j]};
        // This Apply replaces an older record of the trainer (the program overwrote part of it, so it reads Not
        // applied and Remove is refused: Apply is the only way on). A place that still holds what that record
        // wrote has not been touched since: the byte there is our own poke, and what it replaced is the older
        // record's original - taking the byte for the original would leave the poke in memory for good. A place
        // the program changed holds the program's byte, which is what Remove puts back.
        for (const Rec& old : records_[i])
            if (old.loc.page == rec.loc.page && old.loc.off == rec.loc.off && old.final_ == rec.loc.byte)
                rec.original = old.original;
        recs.push_back(rec);
    }

    // Once every location has resolved, the one thing that can refuse a poke is an
    // RZX recording or playback, and it refuses every poke alike: if the first byte
    // lands they all do, and a refusal leaves memory as it was.
    for (const Rec& r : recs) {
        why = poke_byte(r.loc, r.final_);
        if (!why.empty()) return why;
    }
    records_[i] = std::move(recs);
    log_done("applied", t);
    return {};
}

std::string Session::remove(size_t i) {
    if (i >= file_.trainers.size()) return "no such trainer";
    const Trainer& t = file_.trainers[i];
    switch (state(i)) {
    case State::NotApplied:
        return "not applied";

    case State::Applied: {
        for (const Rec& r : records_[i]) {
            const std::string why = poke_byte(r.loc, r.original);
            if (!why.empty()) return why;
        }
        records_[i].clear();
        log_done("removed", t);
        return {};
    }

    case State::AppliedFound: {
        for (const Poke& p : t.pokes)
            if (p.original == 0)
                return at(p.line, "the file gives no original value for address " +
                                      std::to_string(p.address) +
                                      ", so this trainer cannot be removed");
        Plan plan;
        std::string why;
        if (!resolve(t, plan, why)) return why;
        for (size_t j = 0; j < plan.locs.size(); ++j) {
            why = poke_byte(plan.locs[j],
                            static_cast<uint8_t>(t.pokes[plan.first_poke[j]].original));
            if (!why.empty()) return why;
        }
        records_[i].clear();   // a record that no longer verified is moot now
        log_done("removed", t);
        return {};
    }
    }
    return {};
}

std::optional<uint8_t> Session::value_default(size_t i) const {
    if (i >= file_.trainers.size()) return std::nullopt;
    for (const Poke& p : file_.trainers[i].pokes) {
        if (p.value != 256) continue;
        Loc loc;
        std::string why;
        if (!locate(p, dbg_.mmu_slots(), dbg_.machine().type == MachineType::ZX48K, loc, why))
            return std::nullopt;
        return loc.byte;
    }
    return std::nullopt;
}

void Session::forget_applied() {
    for (std::vector<Rec>& r : records_) r.clear();
}

// ---------------------------------------------------------------------------
// From the command line
// ---------------------------------------------------------------------------

namespace {

/// A trainer for a message: `trainer N ('NAME')`, the name cut and escaped to ASCII.
std::string label(size_t number, const Trainer& t) {
    const std::string_view name = t.name;
    return "trainer " + std::to_string(number) + " ('" +
           ascii(name.size() > 40 ? name.substr(0, 40) : name) + (name.size() > 40 ? "..." : "") + "')";
}

}  // namespace

bool parse_picks(std::string_view text, std::vector<Pick>& out, std::string& error) {
    std::vector<Pick> picks;
    auto fail = [&](std::string_view item, const std::string& what) {
        error = "'" + shown(item) + "' " + what;
        return false;
    };
    auto digits = [](std::string_view t) {
        if (t.empty()) return false;
        for (char c : t)
            if (!is_digit(c)) return false;
        return true;
    };
    if (text.empty()) {
        error = "the list is empty";
        return false;
    }
    size_t pos = 0;
    for (;;) {
        const size_t comma = text.find(',', pos);
        const std::string_view item =
            text.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
        if (item.empty()) {
            error = "an entry is empty (a list is like 1,3=9,7)";
            return false;
        }
        const size_t eq = item.find('=');
        const std::string_view num = item.substr(0, eq);
        Pick pick;
        // to_num() saturates at 1000000, which no file can reach in 1 MiB.
        bool ok = digits(num) && to_num(num) >= 1 && to_num(num) < 1000000;
        if (ok) pick.number = static_cast<size_t>(to_num(num));
        if (ok && eq != std::string_view::npos) {
            const std::string_view val = item.substr(eq + 1);
            ok = digits(val) && to_num(val) <= 255;
            if (ok) pick.value = static_cast<uint8_t>(to_num(val));
        }
        if (!ok)
            return fail(item, "is not T or T=VALUE (T a trainer number from 1, VALUE 0-255, "
                              "decimal, no blanks)");
        for (const Pick& q : picks)
            if (q.number == pick.number)
                return fail(item, "names trainer " + std::to_string(pick.number) + " twice");
        picks.push_back(pick);
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    out = std::move(picks);
    return true;
}

std::string make_cli_request(const std::string& path, const std::vector<Pick>& picks,
                             CliRequest& out) {
    CliRequest req;
    req.path  = path;
    req.picks = picks;
    std::string err;
    if (!load_pok_file(path, req.file, err)) return err;
    const size_t count = req.file.trainers.size();
    for (const Pick& p : picks) {
        if (p.number > count)
            return "trainer " + std::to_string(p.number) + ": " + path + " has only " +
                   std::to_string(count) + " trainer(s)";
        const Trainer& t = req.file.trainers[p.number - 1];
        const std::string who = label(p.number, t);
        if (!t.problem.empty()) return who + " is invalid - " + t.problem;
        if (t.asks_value() && !p.value)
            return who + " leaves a value open: give it as " + std::to_string(p.number) +
                   "=VALUE (0-255)";
        if (!t.asks_value() && p.value)
            return who + " leaves no value open: drop the =" + std::to_string(*p.value);
    }
    out = std::move(req);
    return {};
}

std::string run_cli_request(jnext::dbg::Debugger& dbg, const CliRequest& request) {
    Session session(dbg, request.file, request.path);
    for (const Pick& p : request.picks) {
        const size_t i = p.number - 1;
        const std::string who =
            i < request.file.trainers.size() ? label(p.number, request.file.trainers[i])
                                             : "trainer " + std::to_string(p.number);
        // Memory already holds this trainer's bytes (a snapshot taken with it
        // applied, say): the run asked for them to be there, and they are.
        if (session.state(i) == State::AppliedFound) {
            Log::platform()->info("POK: {} is already in memory, left as it is", who);
            continue;
        }
        const std::string why = session.apply(i, p.value);
        if (!why.empty()) return who + ": " + why;
    }
    return {};
}

CliJob::CliJob(CliRequest request, int frames)
    : request_(std::move(request)), delay_(cli::Delay::frames(frames)) {}

bool CliJob::poll(jnext::dbg::Debugger& dbg, bool refresh_60hz) {
    if (delay_.due()) {
        delay_.disarm();
        const std::string why = run_cli_request(dbg, request_);
        if (!why.empty()) {
            Log::platform()->error("--delayed-pok-frames: {}", why);
            return false;
        }
    } else if (delay_.pending()) {
        delay_.tick(refresh_60hz);
    }
    return true;
}

}  // namespace pok
}  // namespace jnext
