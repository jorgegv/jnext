#include "save/state_desc_json.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>

#include "third_party/nlohmann-json/nlohmann/json.hpp"

namespace jnext {
namespace save {
namespace {

using json = nlohmann::json;

/// Lower-case hex, no separators (§6.2). Fixed table rather than `snprintf`
/// per byte: a 2 MB `bytes()` never happens (that is a `blob`), but a 4 608-byte
/// palette does, and the schema pins the string's exact length, so the encoding
/// has to be exactly this and exactly this fast.
std::string to_hex(const uint8_t* d, std::size_t n)
{
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.resize(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        s[2 * i]     = kHex[d[i] >> 4];
        s[2 * i + 1] = kHex[d[i] & 0x0F];
    }
    return s;
}

int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;   // UPPER CASE IS REJECTED: §6.2 says lower-case, and a
                 // permissive reader is how a writer defect survives.
}

std::string dec(uint64_t v)
{
    char b[24];
    std::snprintf(b, sizeof(b), "%" PRIu64, v);
    return b;
}

std::string dec(int64_t v)
{
    char b[24];
    std::snprintf(b, sizeof(b), "%" PRId64, v);
    return b;
}

/// §6.2 — the open-ended sentinel.
constexpr char kOpen[] = "open";

/// Parse a decimal string the FILE supplied. Returns false on anything that is
/// not exactly an optionally-signed run of digits that fits the range: no
/// leading `+`, no whitespace, no `0x`, no trailing junk, no overflow.
/// `strtoll` alone accepts most of those, which is why this does not use it
/// bare.
bool parse_i64(const std::string& s, int64_t& out)
{
    if (s.empty() || s.size() > 20) return false;
    std::size_t i = 0;
    bool neg = false;
    if (s[0] == '-') { neg = true; i = 1; if (s.size() == 1) return false; }
    // A leading zero on a multi-digit number is not canonical output, and
    // accepting it would let two spellings of one value round-trip
    // differently through a byte-diffed schema gate.
    if (s[i] == '0' && s.size() - i > 1) return false;
    uint64_t mag = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        const uint64_t d = static_cast<uint64_t>(s[i] - '0');
        if (mag > (std::numeric_limits<uint64_t>::max() - d) / 10) return false;
        mag = mag * 10 + d;
    }
    const uint64_t limit = neg ? 9223372036854775808ULL : 9223372036854775807ULL;
    if (mag > limit) return false;
    if (neg && mag == 0) return false;   // "-0" is a second spelling of 0
    out = neg ? static_cast<int64_t>(~mag + 1) : static_cast<int64_t>(mag);
    return true;
}

bool parse_u64(const std::string& s, uint64_t& out)
{
    if (s.empty() || s.size() > 20 || s[0] == '-') return false;
    if (s[0] == '0' && s.size() > 1) return false;
    uint64_t mag = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        const uint64_t d = static_cast<uint64_t>(c - '0');
        if (mag > (std::numeric_limits<uint64_t>::max() - d) / 10) return false;
        mag = mag * 10 + d;
    }
    out = mag;
    return true;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────
// JsonWriteDesc
// ─────────────────────────────────────────────────────────────────────────

struct JsonWriteDesc::Impl {
    json obj = json::object();
};

JsonWriteDesc::JsonWriteDesc() : p_(new Impl) {}
JsonWriteDesc::~JsonWriteDesc() = default;

void JsonWriteDesc::do_boolean(const char* n, bool& v, Def<bool>)
{
    p_->obj[n] = v;
}
void JsonWriteDesc::do_u8(const char* n, uint8_t& v, Def<uint8_t>)
{
    p_->obj[n] = static_cast<unsigned>(v);
}
void JsonWriteDesc::do_u16(const char* n, uint16_t& v, Def<uint16_t>)
{
    p_->obj[n] = static_cast<unsigned>(v);
}
void JsonWriteDesc::do_u32(const char* n, uint32_t& v, Def<uint32_t>)
{
    p_->obj[n] = v;
}
void JsonWriteDesc::do_u64(const char* n, uint64_t& v, Def<uint64_t>)
{
    // §6.2/§7.4 — a STRING. The tree already holds `u64` values past 2^53
    // (`monotonic_tstates`), and a JSON number would be silently rounded by
    // every JavaScript reader that ever opens one of these files.
    p_->obj[n] = dec(v);
}
void JsonWriteDesc::do_i32(const char* n, int32_t& v, Def<int32_t>)
{
    p_->obj[n] = v;
}
void JsonWriteDesc::do_i64(const char* n, int64_t& v, Def<int64_t>)
{
    p_->obj[n] = dec(v);
}
void JsonWriteDesc::do_i64_open(const char* n, int64_t& v, Def<int64_t>)
{
    p_->obj[n] = (v == std::numeric_limits<int64_t>::max()) ? std::string(kOpen)
                                                            : dec(v);
}
void JsonWriteDesc::do_enum8(const char* n, uint8_t& v, const EnumNames& e,
                             Def<uint8_t>)
{
    const char* nm = e.name_of(v);
    if (!nm) { fail(n); return; }   // an ordinal the declaration does not have
    p_->obj[n] = std::string(nm);
}

void JsonWriteDesc::bytes(const char* n, uint8_t* data, std::size_t len)
{
    if (!data && len) { fail(n); return; }
    p_->obj[n] = to_hex(data, len);
}

void JsonWriteDesc::blob(const char* n, uint8_t* data, std::size_t len)
{
    // NOT in the JSON (§6.1): the bytes become a ZIP member, and the manifest
    // declaration is what §5.3's chain validates.
    if (!data && len) { fail(n); return; }
    blobs_.push_back({std::string(n), data, len});
}

void JsonWriteDesc::ram_window(const char* n, uint8_t* data, std::size_t len,
                               uint32_t page)
{
    // §9.2's run-time assertion of the STATIC claim.
    if (machine_level() && data == nullptr) { fail(n); return; }
    // §6.1 case 2 — a REFERENCE, stored once. The bytes are already inside
    // `mem/ram.bin`; writing them again is the 131 072-byte duplication
    // §17.0 removes from the binary stream in S5b, and there is no reason to
    // reproduce it in a format that has somewhere honest to put it.
    json ref     = json::object();
    ref["ref"]   = "mem/ram.bin";
    ref["page"]  = page;
    ref["bytes"] = dec(static_cast<uint64_t>(len));
    p_->obj[n]   = ref;
}

void JsonWriteDesc::log(const char* n, LogAccess& entries, std::size_t& count,
                        std::size_t capacity)
{
    // §6.2 — EXACTLY `count` items, never the padded capacity. A snapshot's
    // text carrying 1 024 entries to express three is the thing this primitive
    // exists to avoid; the padding belongs to the binary encoding alone.
    const std::size_t live = (count <= capacity) ? count : capacity;
    json arr = json::array();
    for (std::size_t i = 0; i < live; ++i) {
        json e    = json::object();
        e["line"] = entries.line(i);
        e["value"] = entries.value(i);
        arr.push_back(e);
    }
    p_->obj[n] = arr;
}

void JsonWriteDesc::fifo(const char* n, FifoAccess& ring, FifoElem)
{
    const std::size_t cap  = ring.capacity();
    const std::size_t sz   = ring.size();
    const std::size_t live = (sz <= cap) ? sz : cap;
    json arr = json::array();
    for (std::size_t i = 0; i < live; ++i) arr.push_back(ring.oldest(i));
    p_->obj[n] = arr;
}

void JsonWriteDesc::sentinel(const char*, uint32_t, uint32_t)
{
    // Nothing. Framing, not state (§9.4): the member and key names localise a
    // desync structurally, so a sentinel in the JSON would be a magic number
    // with no job.
}

std::string JsonWriteDesc::str() const
{
    // Deterministic by construction (§16.3): nlohmann's default `json` object
    // is a `std::map`, so keys come out SORTED rather than in hash or
    // insertion order; `dump(2)` has no timestamp, no path and no build id;
    // and the trailing newline makes the file `diff`-friendly and its absence
    // impossible to lose in an editor.
    return p_->obj.dump(2) + "\n";
}

// ─────────────────────────────────────────────────────────────────────────
// JsonReadDesc
// ─────────────────────────────────────────────────────────────────────────

struct JsonReadDesc::Impl {
    json                  obj = json::object();
    std::set<std::string> claimed;
    bool                  parsed = false;
};

namespace {

/// The deepest container nesting a `.jns` state document may have.
///
/// Three is the deepest anything the descriptor can emit: a `log` is
/// `{ key: [ { line, value } ] }`. Sixteen is generous enough that no honest
/// document can reach it and small enough that the DOM a hostile one builds is
/// bounded.
///
/// This is NOT protection against a stack overflow — nlohmann's parser and its
/// DOM destructor are both iterative, and 5 000 000 levels were measured to
/// parse and destroy cleanly on this box. It bounds MEMORY AMPLIFICATION: a
/// member of `[` characters becomes one DOM node per byte, and S1's
/// central-directory cap lets a member be 64 MB. That is the same class as the
/// 167-byte archive that demanded 4.29 GB, one layer down.
constexpr int kMaxDepth = 16;

}  // namespace

JsonReadDesc::JsonReadDesc(const std::string& text) : p_(new Impl)
{
    // Two things the default `json::parse` does NOT do, both of which matter
    // for a file a user was handed.
    //
    // DUPLICATE KEYS. `{"x":1,"x":2}` parses silently to `{"x":2}` — measured.
    // Two implementations may legitimately disagree about which wins, which is
    // exactly why §12.4 refuses duplicate ZIP MEMBER names; a duplicate key is
    // the same ambiguity one level down. It is also NOT something a JSON Schema
    // can catch, whatever validator is used: the duplicate is gone before the
    // validator ever sees the document, so this is the only place it can be
    // refused. (§5.3 lists "a duplicate key" among the things the schema
    // validates. It does not.)
    //
    // DEPTH. See `kMaxDepth`.
    bool dup = false;
    bool deep = false;
    std::vector<std::set<std::string>> seen;
    auto guard = [&](int depth, json::parse_event_t event,
                     json& parsed) -> bool {
        if (depth > kMaxDepth) { deep = true; return false; }
        switch (event) {
            case json::parse_event_t::object_start:
                seen.emplace_back();
                break;
            case json::parse_event_t::key:
                if (!seen.empty() &&
                    !seen.back().insert(parsed.get<std::string>()).second) {
                    dup = true;
                }
                break;
            case json::parse_event_t::object_end:
                if (!seen.empty()) seen.pop_back();
                break;
            default:
                break;
        }
        return true;
    };

    // `allow_exceptions = false`: a malformed document must be a refusal, not
    // a throw through a subsystem's describe_state.
    json j = json::parse(text, guard, /*allow_exceptions=*/false,
                         /*ignore_comments=*/false);
    if (deep) {
        refuse("<document>", "nests deeper than " + std::to_string(kMaxDepth) +
                                 " levels");
        return;
    }
    if (dup) {
        refuse("<document>", "contains a duplicate key");
        return;
    }
    if (j.is_discarded()) {
        refuse("<document>", "is not valid JSON");
        return;
    }
    if (!j.is_object()) {
        refuse("<document>", "is not a JSON object");
        return;
    }
    p_->obj    = std::move(j);
    p_->parsed = true;
}

JsonReadDesc::~JsonReadDesc() = default;

void JsonReadDesc::refuse(const char* name, const std::string& why)
{
    // THE FIRST refusal is the one reported, and the mechanism is `failed()`
    // at the head of every lookup, not a guard here: once one field refuses,
    // every later declaration returns immediately and never calls this. A
    // second `if (!refusal_.empty()) return;` was written here first and a
    // mutation proved it unreachable, so it is gone rather than left as a
    // belt nothing holds up.
    refusal_ = std::string(name ? name : "<unnamed>") + ": " + why;
    fail(refusal_.c_str());
}

std::vector<std::string> JsonReadDesc::unclaimed_keys() const
{
    std::vector<std::string> out;
    if (!p_->parsed) return out;
    for (auto it = p_->obj.begin(); it != p_->obj.end(); ++it) {
        if (p_->claimed.find(it.key()) == p_->claimed.end()) out.push_back(it.key());
    }
    return out;   // already sorted: the underlying object is a std::map
}

namespace {

/// Look a key up, recording the claim. Three outcomes, and the caller must
/// distinguish all three: present, absent, or "reader already failed".
enum class Lookup { Present, Absent, Dead };

}  // namespace

// The body of every scalar read is the same three steps, and writing them out
// nine times is how one of them ends up subtly different. The macro is local
// to this file and expands to code a reader can still step through.
#define JNS_LOOKUP(name, def_expr)                                          \
    if (failed()) return;                                                   \
    p_->claimed.insert(name);                                               \
    auto it = p_->obj.find(name);                                           \
    if (it == p_->obj.end()) {                                              \
        /* §12.2 — a missing key takes its DECLARED default, or is a       \
           refusal when the declaration gave none. */                       \
        def_expr;                                                           \
        return;                                                             \
    }

void JsonReadDesc::do_boolean(const char* n, bool& v, Def<bool> d)
{
    JNS_LOOKUP(n, { if (d.has) v = d.value; else refuse(n, "required key is missing"); })
    if (!it->is_boolean()) { refuse(n, "is not a boolean"); return; }
    v = it->get<bool>();
}

namespace {
/// Every unsigned scalar has the same shape: it must be a JSON number, it must
/// be a non-negative integer, and it must fit the DECLARED width. The width is
/// the declaration's, never the file's.
template <typename T>
bool get_unsigned(const nlohmann::json& j, T& out)
{
    if (!j.is_number_unsigned()) return false;
    const uint64_t raw = j.get<uint64_t>();
    if (raw > static_cast<uint64_t>(std::numeric_limits<T>::max())) return false;
    out = static_cast<T>(raw);
    return true;
}
}  // namespace

void JsonReadDesc::do_u8(const char* n, uint8_t& v, Def<uint8_t> d)
{
    JNS_LOOKUP(n, { if (d.has) v = d.value; else refuse(n, "required key is missing"); })
    if (!get_unsigned(*it, v)) { refuse(n, "is not an integer in 0..255"); return; }
}
void JsonReadDesc::do_u16(const char* n, uint16_t& v, Def<uint16_t> d)
{
    JNS_LOOKUP(n, { if (d.has) v = d.value; else refuse(n, "required key is missing"); })
    if (!get_unsigned(*it, v)) { refuse(n, "is not an integer in 0..65535"); return; }
}
void JsonReadDesc::do_u32(const char* n, uint32_t& v, Def<uint32_t> d)
{
    JNS_LOOKUP(n, { if (d.has) v = d.value; else refuse(n, "required key is missing"); })
    if (!get_unsigned(*it, v)) { refuse(n, "is not an integer in 0..4294967295"); return; }
}
void JsonReadDesc::do_i32(const char* n, int32_t& v, Def<int32_t> d)
{
    JNS_LOOKUP(n, { if (d.has) v = d.value; else refuse(n, "required key is missing"); })
    if (!it->is_number_integer() || it->is_number_float()) {
        refuse(n, "is not an integer"); return;
    }
    const int64_t raw = it->get<int64_t>();
    if (raw < std::numeric_limits<int32_t>::min() ||
        raw > std::numeric_limits<int32_t>::max()) {
        refuse(n, "is outside the range of a signed 32-bit value"); return;
    }
    v = static_cast<int32_t>(raw);
}
void JsonReadDesc::do_u64(const char* n, uint64_t& v, Def<uint64_t> d)
{
    JNS_LOOKUP(n, { if (d.has) v = d.value; else refuse(n, "required key is missing"); })
    // A STRING, per §6.2. A JSON number here is a refusal rather than a
    // convenience: accepting both spellings makes the schema's `pattern`
    // decorative and re-opens the 2^53 rounding §7.4 closes.
    if (!it->is_string()) { refuse(n, "is not a decimal string"); return; }
    uint64_t parsed = 0;
    if (!parse_u64(it->get<std::string>(), parsed)) {
        refuse(n, "is not a canonical unsigned decimal string"); return;
    }
    v = parsed;
}
void JsonReadDesc::do_i64(const char* n, int64_t& v, Def<int64_t> d)
{
    JNS_LOOKUP(n, { if (d.has) v = d.value; else refuse(n, "required key is missing"); })
    if (!it->is_string()) { refuse(n, "is not a decimal string"); return; }
    int64_t parsed = 0;
    if (!parse_i64(it->get<std::string>(), parsed)) {
        refuse(n, "is not a canonical signed decimal string"); return;
    }
    v = parsed;
}
void JsonReadDesc::do_i64_open(const char* n, int64_t& v, Def<int64_t> d)
{
    JNS_LOOKUP(n, { if (d.has) v = d.value; else refuse(n, "required key is missing"); })
    if (!it->is_string()) { refuse(n, "is not a decimal string or \"open\""); return; }
    const std::string s = it->get<std::string>();
    if (s == kOpen) { v = std::numeric_limits<int64_t>::max(); return; }
    int64_t parsed = 0;
    if (!parse_i64(s, parsed)) {
        refuse(n, "is not a canonical signed decimal string or \"open\""); return;
    }
    v = parsed;
}

void JsonReadDesc::do_enum8(const char* n, uint8_t& v, const EnumNames& e,
                            Def<uint8_t> d)
{
    JNS_LOOKUP(n, { if (d.has) v = d.value; else refuse(n, "required key is missing"); })
    if (!it->is_string()) { refuse(n, "is not an enum name"); return; }
    const int ord = e.ordinal_of(it->get<std::string>().c_str());
    // §16.1 — an unknown name REFUSES rather than defaulting. A wrong FSM
    // state is not a safe default: it is a machine that never existed.
    if (ord < 0) { refuse(n, "is not a name this build declares"); return; }
    v = static_cast<uint8_t>(ord);
}

void JsonReadDesc::bytes(const char* n, uint8_t* data, std::size_t len)
{
    JNS_LOOKUP(n, { refuse(n, "required key is missing"); })
    if (!it->is_string()) { refuse(n, "is not a hex string"); return; }
    const std::string s = it->get<std::string>();
    // The DECLARATION fixes the length. A file that disagrees is refused, and
    // nothing here is sized by the file — which is the whole lesson of the
    // 167-byte archive that demanded 4.29 GB.
    if (s.size() != len * 2) {
        refuse(n, "hex string is " + std::to_string(s.size()) +
                      " characters, the declaration says " +
                      std::to_string(len * 2));
        return;
    }
    if (!data && len) { refuse(n, "has no destination buffer"); return; }
    for (std::size_t i = 0; i < len; ++i) {
        const int hi = hex_nibble(s[2 * i]);
        const int lo = hex_nibble(s[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            refuse(n, "is not lower-case hexadecimal"); return;
        }
        data[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
}

void JsonReadDesc::blob(const char* n, uint8_t*, std::size_t)
{
    // The bytes come from a ZIP member, not from here (§6.1). The key is
    // claimed so it is not reported as unknown, and a document that DOES carry
    // it is refused: a blob inlined into the JSON is a file our writer cannot
    // produce, and accepting it would mean two places to look for the bytes.
    if (failed()) return;
    p_->claimed.insert(n);
    if (p_->obj.find(n) != p_->obj.end()) {
        refuse(n, "is a blob member and must not appear in the JSON");
    }
}

void JsonReadDesc::ram_window(const char* n, uint8_t* data, std::size_t len,
                              uint32_t page)
{
    JNS_LOOKUP(n, { refuse(n, "required key is missing"); })
    if (machine_level() && data == nullptr) {
        refuse(n, "is declared a RAM window but nothing backs it"); return;
    }
    if (!it->is_object()) { refuse(n, "is not a RAM reference object"); return; }
    const auto& o = *it;
    auto r = o.find("ref");
    auto pg = o.find("page");
    auto by = o.find("bytes");
    if (r == o.end() || pg == o.end() || by == o.end()) {
        refuse(n, "is missing one of ref/page/bytes"); return;
    }
    if (!r->is_string() || r->get<std::string>() != "mem/ram.bin") {
        refuse(n, "does not reference mem/ram.bin"); return;
    }
    uint32_t got_page = 0;
    if (!get_unsigned(*pg, got_page) || got_page != page) {
        refuse(n, "references a different RAM page than the declaration"); return;
    }
    uint64_t got_bytes = 0;
    if (!by->is_string() || !parse_u64(by->get<std::string>(), got_bytes) ||
        got_bytes != static_cast<uint64_t>(len)) {
        refuse(n, "declares a length the declaration disagrees with"); return;
    }
    // Deliberately no copy: the caller has already restored `mem/ram.bin`, and
    // the window aliases it. This is exactly what makes it a reference.
}

void JsonReadDesc::log(const char* n, LogAccess& entries, std::size_t& count,
                       std::size_t capacity)
{
    JNS_LOOKUP(n, { refuse(n, "required key is missing"); })
    if (!it->is_array()) { refuse(n, "is not an array"); return; }
    // BOUNDED BY THE DECLARATION. An array of 4 billion entries in the file is
    // refused before a single one is stored, and nothing is allocated for it:
    // `entries` is the subsystem's own fixed array.
    if (it->size() > capacity) {
        refuse(n, "has " + std::to_string(it->size()) +
                      " entries, the declared capacity is " +
                      std::to_string(capacity));
        return;
    }
    std::size_t i = 0;
    for (const auto& e : *it) {
        if (!e.is_object()) { refuse(n, "entry is not an object"); return; }
        auto l = e.find("line");
        auto v = e.find("value");
        if (l == e.end() || v == e.end()) {
            refuse(n, "entry is missing line or value"); return;
        }
        uint16_t line = 0;
        uint8_t  val  = 0;
        if (!get_unsigned(*l, line)) { refuse(n, "entry line is out of range"); return; }
        if (!get_unsigned(*v, val))  { refuse(n, "entry value is out of range"); return; }
        entries.set(i++, line, val);
    }
    count = i;
    // The tail past `count` is left untouched, and the two encodings therefore
    // differ in it: the binary stream carries the stale entries and restores
    // them, the JSON carries only `count` items and cannot. That is harmless
    // BY CONTRACT, not by luck — `ula.cpp:1622-1625` documents entries past
    // the count as stale and ignored, and the renderer never reads past
    // `port_ff_count_`. Zeroing them here would be a third behaviour, agreeing
    // with neither.
}

void JsonReadDesc::fifo(const char* n, FifoAccess& ring, FifoElem elem)
{
    JNS_LOOKUP(n, { refuse(n, "required key is missing"); })
    if (!it->is_array()) { refuse(n, "is not an array"); return; }
    const std::size_t cap = ring.capacity();
    if (it->size() > cap) {
        refuse(n, "has " + std::to_string(it->size()) +
                      " entries, the declared capacity is " + std::to_string(cap));
        return;
    }
    // Validate EVERY element before mutating the ring: a refusal half-way
    // through would leave the FIFO holding a prefix of a rejected file.
    std::vector<uint16_t> vals;
    vals.reserve(it->size());
    for (const auto& e : *it) {
        uint16_t v = 0;
        if (elem == FifoElem::U8) {
            uint8_t b = 0;
            if (!get_unsigned(e, b)) { refuse(n, "entry is not in 0..255"); return; }
            v = b;
        } else {
            if (!get_unsigned(e, v)) { refuse(n, "entry is not in 0..65535"); return; }
        }
        vals.push_back(v);
    }
    ring.reset();
    for (uint16_t v : vals) ring.push(v);
}

void JsonReadDesc::sentinel(const char*, uint32_t, uint32_t)
{
    // Nothing: framing, not state (§9.4).
}

#undef JNS_LOOKUP

}  // namespace save
}  // namespace jnext
