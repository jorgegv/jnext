#include "save/state_desc_schema.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <limits>
#include <map>

#include "third_party/nlohmann-json/nlohmann/json.hpp"

namespace jnext {
namespace save {
namespace {

using json = nlohmann::json;

/// §6.2 — the canonical decimal-string patterns. They are the SAME grammar
/// `JsonReadDesc`'s `parse_u64`/`parse_i64` accept: one spelling per value, no
/// leading zeros, no `+`, no `-0`. A looser pattern here would let a file the
/// reader refuses pass validation, which makes the validator lie in the more
/// dangerous direction.
constexpr char kU64Pattern[] = "^(0|[1-9][0-9]*)$";
constexpr char kI64Pattern[] = "^(0|-?[1-9][0-9]*)$";

json uint_schema(uint64_t max)
{
    json s     = json::object();
    s["type"]  = "integer";
    s["minimum"] = 0;
    s["maximum"] = max;
    return s;
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

}  // namespace

struct SchemaDesc::Impl {
    /// std::map, so both the properties object and the required list come out
    /// sorted whatever order the declaration used (§16.3).
    std::map<std::string, json> props;
    std::vector<std::string>    required;

    void add(const char* name, json s) { props[name] = std::move(s); }
    void require(const char* name) { required.push_back(name); }
};

SchemaDesc::SchemaDesc() : p_(new Impl) {}
SchemaDesc::~SchemaDesc() = default;

// ── Scalars ──────────────────────────────────────────────────────────────

void SchemaDesc::do_boolean(const char* n, bool&, Def<bool> d)
{
    json s    = json::object();
    s["type"] = "boolean";
    if (d.has) s["default"] = d.value; else p_->require(n);
    p_->add(n, std::move(s));
}

void SchemaDesc::do_u8(const char* n, uint8_t&, Def<uint8_t> d)
{
    json s = uint_schema(255);
    if (d.has) s["default"] = static_cast<unsigned>(d.value); else p_->require(n);
    p_->add(n, std::move(s));
}

void SchemaDesc::do_u16(const char* n, uint16_t&, Def<uint16_t> d)
{
    json s = uint_schema(65535);
    if (d.has) s["default"] = static_cast<unsigned>(d.value); else p_->require(n);
    p_->add(n, std::move(s));
}

void SchemaDesc::do_u32(const char* n, uint32_t&, Def<uint32_t> d)
{
    json s = uint_schema(4294967295ULL);
    if (d.has) s["default"] = d.value; else p_->require(n);
    p_->add(n, std::move(s));
}

void SchemaDesc::do_i32(const char* n, int32_t&, Def<int32_t> d)
{
    json s       = json::object();
    s["type"]    = "integer";
    s["minimum"] = std::numeric_limits<int32_t>::min();
    s["maximum"] = std::numeric_limits<int32_t>::max();
    if (d.has) s["default"] = d.value; else p_->require(n);
    p_->add(n, std::move(s));
}

void SchemaDesc::do_u64(const char* n, uint64_t&, Def<uint64_t> d)
{
    // A STRING, not a number: §7.4 — the values exceed 2^53 and a JSON number
    // would be silently rounded by a JavaScript reader.
    json s       = json::object();
    s["type"]    = "string";
    s["pattern"] = kU64Pattern;
    if (d.has) s["default"] = dec(d.value); else p_->require(n);
    p_->add(n, std::move(s));
}

void SchemaDesc::do_i64(const char* n, int64_t&, Def<int64_t> d)
{
    json s       = json::object();
    s["type"]    = "string";
    s["pattern"] = kI64Pattern;
    if (d.has) s["default"] = dec(d.value); else p_->require(n);
    p_->add(n, std::move(s));
}

void SchemaDesc::do_i64_open(const char* n, int64_t&, Def<int64_t> d)
{
    // §6.2 — the open-ended sentinel is an `enum` alongside the numeric
    // pattern, so `"open"` validates and `"opne"` does not.
    json numeric     = json::object();
    numeric["pattern"] = kI64Pattern;
    json sentinel    = json::object();
    sentinel["enum"] = json::array({"open"});

    json s     = json::object();
    s["type"]  = "string";
    s["anyOf"] = json::array({numeric, sentinel});
    if (d.has) {
        s["default"] = (d.value == std::numeric_limits<int64_t>::max())
                           ? std::string("open")
                           : dec(d.value);
    } else {
        p_->require(n);
    }
    p_->add(n, std::move(s));
}

void SchemaDesc::do_enum8(const char* n, uint8_t&, const EnumNames& e,
                          Def<uint8_t> d)
{
    // A CLOSED set of names. An FSM renumbering becomes a visible name change
    // in the schema diff rather than a silent re-interpretation of old files
    // (§6.2) — which is the single biggest thing the JSON encoding buys.
    json names = json::array();
    for (std::size_t i = 0; i < e.count; ++i) {
        if (e.names[i]) names.push_back(e.names[i]);
    }
    json s    = json::object();
    s["type"] = "string";
    s["enum"] = names;
    if (d.has) {
        const char* dn = e.name_of(d.value);
        if (!dn) { fail(n); return; }   // a default outside its own name set
        s["default"] = std::string(dn);
    } else {
        p_->require(n);
    }
    p_->add(n, std::move(s));
}

// ── Aggregates ───────────────────────────────────────────────────────────

void SchemaDesc::bytes(const char* n, uint8_t*, std::size_t len)
{
    // The EXACT length as a literal in the pattern (§5.3): a 2 KB fixed array
    // is length-checked by the schema itself, so a descriptor that silently
    // resized a buffer fails validation instead of re-describing itself.
    json s       = json::object();
    s["type"]    = "string";
    s["pattern"] = "^[0-9a-f]{" + std::to_string(len * 2) + "}$";
    p_->add(n, std::move(s));
    p_->require(n);
}

void SchemaDesc::blob(const char* n, uint8_t*, std::size_t)
{
    // No property: the bytes are a ZIP member. The name goes to the generator
    // so the manifest's declaration of it can be constrained (§5.3).
    blob_keys_.push_back(n);
}

void SchemaDesc::ram_window(const char* n, uint8_t*, std::size_t len,
                            uint32_t page)
{
    json ref      = json::object();
    ref["const"]  = "mem/ram.bin";
    json pg       = json::object();
    pg["const"]   = page;
    json by       = json::object();
    by["const"]   = dec(static_cast<uint64_t>(len));

    json props         = json::object();
    props["ref"]       = ref;
    props["page"]      = pg;
    props["bytes"]     = by;

    json s                     = json::object();
    s["type"]                  = "object";
    s["additionalProperties"]  = false;
    s["required"]              = json::array({"bytes", "page", "ref"});
    s["properties"]            = props;
    p_->add(n, std::move(s));
    p_->require(n);
}

void SchemaDesc::log(const char* n, LogAccess&, std::size_t&,
                     std::size_t capacity)
{
    json entry_props    = json::object();
    entry_props["line"]  = uint_schema(65535);
    entry_props["value"] = uint_schema(255);

    json entry                    = json::object();
    entry["type"]                 = "object";
    entry["additionalProperties"] = false;
    entry["required"]             = json::array({"line", "value"});
    entry["properties"]           = entry_props;

    // `maxItems` is the BINARY capacity (§6.2): the JSON carries exactly the
    // live count, and the capacity is the bound a file may not exceed.
    json s        = json::object();
    s["type"]     = "array";
    s["maxItems"] = capacity;
    s["items"]    = entry;
    p_->add(n, std::move(s));
    p_->require(n);
}

void SchemaDesc::fifo(const char* n, FifoAccess& ring, FifoElem elem)
{
    json s        = json::object();
    s["type"]     = "array";
    s["maxItems"] = ring.capacity();
    s["items"]    = uint_schema(elem == FifoElem::U8 ? 255 : 65535);
    p_->add(n, std::move(s));
    p_->require(n);
}

void SchemaDesc::sentinel(const char*, uint32_t, uint32_t)
{
    // Nothing: framing, not state (§9.4). A sentinel in the schema would
    // describe a key no `.jns` contains.
}

std::string SchemaDesc::str() const
{
    json props = json::object();
    for (const auto& kv : p_->props) props[kv.first] = kv.second;

    std::vector<std::string> req = p_->required;
    std::sort(req.begin(), req.end());

    json s                    = json::object();
    s["type"]                 = "object";
    // Stricter than the READER's §12.2 rule, deliberately — see the header.
    s["additionalProperties"] = false;
    s["properties"]           = props;
    s["required"]             = req;
    return s.dump(2) + "\n";
}

// ── SchemaRegistry ───────────────────────────────────────────────────────

namespace {
/// Function-local, so registration from a static initialiser in another
/// translation unit cannot race the container's own construction.
std::map<std::string, SchemaRegistry::Describe>& registry()
{
    static std::map<std::string, SchemaRegistry::Describe> r;
    return r;
}
}  // namespace

void SchemaRegistry::register_subsystem(const char* member, Describe fn)
{
    registry()[member] = fn;
}

std::vector<SchemaRegistry::Entry> SchemaRegistry::entries()
{
    std::vector<Entry> out;
    for (const auto& kv : registry()) out.push_back({kv.first, kv.second});
    return out;   // std::map — already sorted by member name
}

}  // namespace save
}  // namespace jnext
