#include "debugger/breakpoint_model.h"
#include "debugger/memory_text.h"

#include <algorithm>
#include <memory>
#include <set>

#include "script/expr_compiler.h"

using jnext::dbg::Access;
using jnext::dbg::EventKind;
using jnext::dbg::EventKindMask;
using jnext::dbg::Subscription;
using jnext::dbg::SubscriptionInfo;
using jnext::dbg::kind_bit;

namespace {

// The three kinds the GUI creates. A master-switch flip notifies all three,
// as BreakpointSet's did (it notified both of its halves).
constexpr EventKindMask kGuiKinds =
    kind_bit(EventKind::Execute) | kind_bit(EventKind::Mem) | kind_bit(EventKind::Port);

using jnext::script::BpSpec;
using jnext::script::bp_kind_name;
using jnext::script::bp_port_mask_for;

constexpr bool same_kind(int a, int b) { return a == b; }
static_assert(same_kind(BreakpointModel::Execute, BpSpec::Execute) && same_kind(BreakpointModel::Read, BpSpec::Read) &&
              same_kind(BreakpointModel::Write, BpSpec::Write) &&
              same_kind(BreakpointModel::ReadWrite, BpSpec::ReadWrite) &&
              same_kind(BreakpointModel::IoRead, BpSpec::IoRead) && same_kind(BreakpointModel::IoWrite, BpSpec::IoWrite) &&
              same_kind(BreakpointModel::Interrupt, BpSpec::Interrupt) && same_kind(BreakpointModel::Nmi, BpSpec::Nmi),
              "BreakpointModel::Type and the codec's BpSpec::Kind are one numbering");

// The subscription one PLAIN GUI breakpoint is: a single address, no condition,
// no handler, not once, the Stop action (REQ-qt-13d). READ_WRITE is ONE row with
// both access bits (REQ-qt-13c). Interrupt and NMI are `IntAck` / `Nmi` with the
// default filter (they have no address).
Subscription sub_for(int type, uint16_t addr, jnext::dbg::MemSpace space) {
    Subscription s;
    s.action = jnext::dbg::Action::Stop;
    switch (type) {
        case BreakpointModel::Execute:
            s.kind        = EventKind::Execute;
            s.filter.lo   = s.filter.hi = addr;
            s.filter.space = space;   // E23-09; cpu() = a logical breakpoint
            break;
        case BreakpointModel::Read:
        case BreakpointModel::Write:
        case BreakpointModel::ReadWrite:
            s.kind      = EventKind::Mem;
            s.filter.lo = s.filter.hi = addr;
            s.access    = type == BreakpointModel::Read  ? Access::Read
                        : type == BreakpointModel::Write ? Access::Write
                                                         : Access::ReadWrite;
            break;
        case BreakpointModel::IoRead:
        case BreakpointModel::IoWrite:
            s.kind              = EventKind::Port;
            s.filter.port_mask  = bp_port_mask_for(addr);
            s.filter.port_value = addr;
            s.access = type == BreakpointModel::IoRead ? Access::Read : Access::Write;
            break;
        case BreakpointModel::Interrupt:
            s.kind = EventKind::IntAck;
            break;
        case BreakpointModel::Nmi:
            s.kind = EventKind::Nmi;
            break;
    }
    return s;
}

// The inverse: which of the types this listed subscription IS, or -1 when it is
// something the Add dialog cannot express. Only such a subscription can be this
// GUI's own editable row. `counted`: it is one of this GUI's count-only ones
// (Continue plus a handler); any other handler is not the dialog's.
int type_of(const SubscriptionInfo& si, bool counted, bool conditional) {
    const auto& f = si.filter;
    // A condition or a handler is the dialog's only when this GUI put it there (`conditional`, `counted`:
    // its own extras); another client's is something the dialog cannot express.
    if (si.has_condition != conditional || si.once ||
        si.has_handler != counted ||
        si.action != (counted ? jnext::dbg::Action::Continue : jnext::dbg::Action::Stop) ||
        !f.pages.empty() || f.source != jnext::dbg::EventSource::Any)
        return -1;
    // The legacy page qualifier (DZRP, the DSL) is not the Add dialog's.
    if (f.page != jnext::dbg::PAGE_ANY) return -1;
    // A space qualifier is expressible on an Execute breakpoint only (E23-09).
    if (f.space != jnext::dbg::MemSpace::cpu() && si.kind != EventKind::Execute) return -1;
    switch (si.kind) {
        case EventKind::Execute:
            // A range is always unqualified: a space-qualified one is not the dialog's.
            return f.lo == f.hi || f.space == jnext::dbg::MemSpace::cpu() ? BreakpointModel::Execute : -1;
        case EventKind::Mem:
            if (si.access == Access::Read)      return BreakpointModel::Read;
            if (si.access == Access::Write)     return BreakpointModel::Write;
            if (si.access == Access::ReadWrite) return BreakpointModel::ReadWrite;
            return -1;
        case EventKind::Port:
            // Any mask the value fits under (and the backend accepts): the dialog's Mask field.
            if (f.port_value & ~f.port_mask) return -1;
            if (si.access == Access::Read)  return BreakpointModel::IoRead;
            if (si.access == Access::Write) return BreakpointModel::IoWrite;
            return -1;
        case EventKind::IntAck: return BreakpointModel::Interrupt;
        case EventKind::Nmi:    return BreakpointModel::Nmi;
        default:
            return -1;
    }
}

// The Address column's qualifier, after "$ADDR": nothing for a logical
// breakpoint, otherwise the memory the location is qualified with, through the
// long-address formatter (GH #360: `P04`, `ROM1`, `DIVROM`, or the physical
// text). Another client's legacy page qualifier is spelled apart, "@pp".
QString space_suffix(jnext::dbg::MemSpace sp, uint16_t addr, jnext::dbg::AddressStyle st, bool next) {
    if (sp == jnext::dbg::MemSpace::cpu()) return QString();
    return QStringLiteral(" ") + QString::fromStdString(jnext::dbg::space_text(sp, addr, st, next));
}

// The Type column for a row the Add dialog could not have made.
QString kind_text(const SubscriptionInfo& si) {
    const char* rw = si.access == Access::Read    ? "Read"
                   : si.access == Access::Write   ? "Write"
                                                  : "Read/Write";
    switch (si.kind) {
        case EventKind::Execute:      return QStringLiteral("Execute");
        case EventKind::Mem:          return QString::fromLatin1(rw);
        case EventKind::Port:         return QStringLiteral("IO %1").arg(QLatin1String(rw));
        case EventKind::NextRegWrite: return QStringLiteral("NextREG Write");
        case EventKind::Frame:        return QStringLiteral("Frame");
        case EventKind::Scanline:     return QStringLiteral("Scanline");
        case EventKind::Cycle:        return QStringLiteral("Cycle");
        case EventKind::Reset:        return QStringLiteral("Reset");
        case EventKind::IntAck:       return QStringLiteral("Interrupt");
        case EventKind::Nmi:          return QStringLiteral("NMI");
        case EventKind::Magic:        return QStringLiteral("Magic");
        case EventKind::Host:         return QStringLiteral("Host");
        case EventKind::Copper:       return QStringLiteral("Copper");
        case EventKind::Dma:          return QStringLiteral("DMA");
        default:                      return QStringLiteral("?");
    }
}

}  // namespace

BreakpointModel::BreakpointModel(jnext::dbg::Debugger& dbg, QObject* parent)
    : QObject(parent), dbg_(dbg) {
    // REQ-qt-32 — the OBSERVER: a client that owns the GUI's breakpoints for as
    // long as the GUI exists, and arms nothing (see the class comment).
    client_ = dbg_.attach(jnext::dbg::ClientInfo{"Qt GUI", jnext::dbg::ClientKind::Gui,
                                                 /*observer=*/true})
                  .value;
    dbg_.set_listener(client_, this);
    publish_();
}

BreakpointModel::~BreakpointModel() {
    dbg_.set_listener(client_, nullptr);
    // SES-01 — takes the GUI's breakpoints with it.
    dbg_.detach(client_);
}

void BreakpointModel::publish_() {
    std::vector<Row>    rows;
    std::vector<Member> members;
    std::vector<Seen>   seen;
    std::set<jnext::dbg::EventId> listed;
    for (const SubscriptionInfo& si : dbg_.subscriptions(/*include_transient=*/false)) {
        listed.insert(si.id);
        Row r;
        r.id      = si.id;
        const auto ex = extras_.find(si.id);
        const bool counted = si.owner == client_ && ex != extras_.end() && ex->second.count_only;
        const bool conditional = si.owner == client_ && ex != extras_.end() && !ex->second.condition.empty();
        r.type    = type_of(si, counted, conditional);
        r.enabled = si.enabled;
        r.live    = si.live;
        r.own     = si.owner == client_ && r.type >= 0;
        r.count_only = r.own && counted;
        const bool port = si.kind == EventKind::Port;
        const bool addressless = r.type == Interrupt || r.type == Nmi;
        r.addr = addressless ? 0 : port ? si.filter.port_value : si.filter.lo;
        r.hi   = port || addressless ? r.addr : si.filter.hi;
        r.mask = port ? si.filter.port_mask : bp_port_mask_for(r.addr);
        r.conditional = si.has_condition;
        if (r.own && conditional) {
            r.condition = QString::fromStdString(ex->second.condition);
        }
        if (si.kind == EventKind::Execute) r.space = si.filter.space;
        if (r.own)
            members.push_back(Member{si.id, r.type, r.addr, r.space, si.enabled, si.live, si.group, r.count_only,
                                     r.hi, r.mask, r.own && conditional ? ex->second.condition : std::string()});
        // E23-09 -- the members of one source breakpoint are ONE row: the
        // lowest address's, enabled only while every location is, live while
        // any is.
        if (si.group != jnext::dbg::GROUP_NONE && r.type == Execute) {
            const auto g = std::find_if(rows.begin(), rows.end(), [&](const Row& o) {
                return o.group == si.group;
            });
            if (g != rows.end()) {
                ++g->locations;
                g->enabled = g->enabled && si.enabled;
                g->live    = g->live || si.live;
                if (r.addr < g->addr) {
                    g->addr      = r.addr;
                    g->space     = r.space;
                    g->addr_text = address_text(r.addr, r.space, style_, memtext::next_layout(&dbg_));
                }
                seen.push_back(Seen{si.id, si.kind, si.enabled, si.live});
                continue;
            }
            r.group = si.group;
        }
        if (r.type >= 0) {
            r.type_text = QString::fromLatin1(bp_kind_name(static_cast<BpSpec::Kind>(r.type)));
            if (r.count_only) r.type_text += QStringLiteral(" (count only)");
            // The same texts another client's range or masked port had before the dialog could make
            // one: `$8000-$80FF`, `$00FE/FFFF`.
            if (addressless) {}
            else if (port && r.mask != bp_port_mask_for(r.addr))
                r.addr_text = QString::asprintf("$%04X/%04X", r.addr, r.mask);
            else if (r.hi != r.addr)
                r.addr_text = QString::asprintf("$%04X-$%04X", r.addr, r.hi);
            else
                r.addr_text = address_text(r.addr, r.space, style_, memtext::next_layout(&dbg_));
        } else {
            r.type_text = kind_text(si);
            if (port)
                r.addr_text = QString::asprintf("$%04X/%04X", si.filter.port_value,
                                                si.filter.port_mask);
            else if (si.kind == EventKind::Execute || si.kind == EventKind::Mem)
                r.addr_text = si.filter.lo == si.filter.hi
                                  ? QString::asprintf("$%04X", si.filter.lo)
                                  : QString::asprintf("$%04X-$%04X", si.filter.lo,
                                                      si.filter.hi);
            // Another client's legacy page qualifier (DZRP, the DSL) reads as it did.
            if (si.kind == EventKind::Execute && si.filter.page != jnext::dbg::PAGE_ANY)
                r.addr_text += QString::asprintf(" @%02X", si.filter.page);
            if (si.kind == EventKind::Execute) r.addr_text += space_suffix(si.filter.space, r.addr, style_, memtext::next_layout(&dbg_));
        }
        // REQ-qt-13d — another client's row is listed, read-only, and says
        // whose it is: the id is the one the backend's ATTACH log line names.
        if (!r.own)
            r.type_text += QStringLiteral(" (client %1)").arg(si.owner);
        rows.push_back(std::move(r));
        seen.push_back(Seen{si.id, si.kind, si.enabled, si.live});
    }
    // By address; at one address the Execute row first, the rest in creation
    // order (the listing's) — the order BreakpointSet's panel showed.
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.addr != b.addr) return a.addr < b.addr;
        return (a.type == Execute) && (b.type != Execute);
    });

    EventKindMask kinds = 0;
    const bool master = dbg_.master_enabled();
    if (master != master_seen_) kinds |= kGuiKinds;
    for (const Seen& s : seen) {
        const auto it = std::find_if(seen_.begin(), seen_.end(),
                                     [&](const Seen& o) { return o.id == s.id; });
        if (it == seen_.end() || it->enabled != s.enabled || it->live != s.live)
            kinds |= kind_bit(s.kind);
    }
    for (const Seen& o : seen_) {
        const auto it = std::find_if(seen.begin(), seen.end(),
                                     [&](const Seen& s) { return s.id == o.id; });
        if (it == seen.end()) kinds |= kind_bit(o.kind);
    }

    for (auto it = group_lines_.begin(); it != group_lines_.end();) {
        const bool listed = std::any_of(members.begin(), members.end(), [&](const Member& m) {
            return m.group == it->first;
        });
        it = listed ? std::next(it) : group_lines_.erase(it);
    }

    // A count and an extra live exactly as long as their subscription.
    for (auto it = extras_.begin(); it != extras_.end();)
        it = listed.count(it->first) ? std::next(it) : extras_.erase(it);
    for (auto it = hits_.begin(); it != hits_.end();)
        it = listed.count(it->first) ? std::next(it) : hits_.erase(it);

    rows_        = std::move(rows);
    members_     = std::move(members);
    seen_        = std::move(seen);
    master_seen_ = master;
    if (kinds) emit changed(kinds);
}

void BreakpointModel::sync() {
    if (dirty_) {
        dirty_ = false;
        publish_();
    }
    if (hits_dirty_) {
        hits_dirty_ = false;
        emit hits_changed();
    }
}

// GH #372 - a Stop breakpoint is counted here, from what the boundary matched:
// it stays a static Stop with no handler. (A count-only one counts in its own
// handler and never stops, so it is not in `matched` as a stop.) Only this GUI's
// own ids count.
void BreakpointModel::on_paused(const jnext::dbg::PausedInfo& info) {
    for (const jnext::dbg::Hit& h : info.matched)
        for (const Member& m : members_)
            if (m.id == h.event_id && !m.count_only) {
                ++hits_[m.id];
                hits_dirty_ = true;
                break;
            }
}

uint64_t BreakpointModel::hits(const Row& row) const {
    if (!row.own) return 0;
    uint64_t n = 0;
    for (const Member& m : members_) {
        if (row.group != jnext::dbg::GROUP_NONE ? m.group != row.group : m.id != row.id) continue;
        const auto h = hits_.find(m.id);
        if (h != hits_.end()) n += h->second;
    }
    return n;
}

QString BreakpointModel::condition_error(const Row& row) const {
    if (!row.own || row.condition.isEmpty()) return QString();
    for (const Member& m : members_) {
        if (row.group != jnext::dbg::GROUP_NONE ? m.group != row.group : m.id != row.id) continue;
        const auto x = extras_.find(m.id);
        if (x != extras_.end() && x->second.error && !x->second.error->empty())
            return QString::fromStdString(*x->second.error);
    }
    return QString();
}

void BreakpointModel::reset_hits() {
    hits_.clear();
    hits_dirty_ = false;
    emit hits_changed();
}

// The (type, address, space) API means the PLAIN breakpoint: one address, the address rule's port mask, no
// condition. A range, a masked port or a conditional one at the same address is another breakpoint.
bool BreakpointModel::plain_(const Member& m) {
    return m.hi == m.addr && m.mask == bp_port_mask_for(m.addr) && m.condition.empty();
}

const BreakpointModel::Member* BreakpointModel::find_own_(int type, uint16_t addr,
                                                          jnext::dbg::MemSpace space,
                                                          bool count_only) const {
    for (const Member& m : members_)
        if (m.type == type && m.addr == addr && m.space == space && m.count_only == count_only && plain_(m)) return &m;
    return nullptr;
}

// The full-spec identity: kind, address, range end, space, mask, condition and count-only.
const BreakpointModel::Member* BreakpointModel::find_spec_(const BpSpec& s) const {
    for (const Member& m : members_)
        if (m.type == s.kind && m.addr == s.addr && m.hi == jnext::script::bp_effective_hi(s) && m.space == s.space &&
            m.mask == jnext::script::bp_effective_mask(s) && m.condition == s.condition && m.count_only == s.count_only)
            return &m;
    return nullptr;
}

std::vector<jnext::dbg::EventId> BreakpointModel::group_ids_(jnext::dbg::GroupId g) const {
    std::vector<jnext::dbg::EventId> ids;
    for (const SubscriptionInfo& si : dbg_.subscriptions(/*include_transient=*/false))
        if (si.owner == client_ && si.group == g) ids.push_back(si.id);
    return ids;
}

// The subscription a breakpoint is, plain or count-only. A count-only one is
// `Continue` plus this model's counting handler, exactly what the script engine
// registers for `do end`; the handler cannot outlive the model, because the
// destructor detaches the client, which removes its subscriptions (SES-01).
bool BreakpointModel::build_(const BpSpec& spec, Subscription& out, std::shared_ptr<std::string>& error,
                             std::string* why) {
    Subscription s = sub_for(spec.kind, spec.addr, spec.space);
    const bool io = spec.kind == BpSpec::IoRead || spec.kind == BpSpec::IoWrite;
    // A range (never qualified with a memory) and a port mask that is not the address rule.
    if (!io && spec.kind != BpSpec::Interrupt && spec.kind != BpSpec::Nmi && jnext::script::bp_effective_hi(spec) != spec.addr) {
        s.filter.lo    = spec.addr;
        s.filter.hi    = jnext::script::bp_effective_hi(spec);
        s.filter.space = jnext::dbg::MemSpace::cpu();
    }
    if (io) s.filter.port_mask = jnext::script::bp_effective_mask(spec);
    // A condition: compiled in the kind's payload scope, with the run-time failure kept for the panel.
    if (!spec.condition.empty()) {
        error = std::make_shared<std::string>();
        jnext::script::CompileOptions opts;
        opts.symbols = jnext::script::symbols_of(dbg_);
        const std::shared_ptr<std::string> sink = error;
        opts.on_runtime_error = [this, sink](const jnext::script::Diagnostic& d) {
            // Logged when this breakpoint's error text CHANGES, not on every evaluation: a failing condition on
            // a hot breakpoint would otherwise write thousands of lines a frame (the script engine logs once too).
            const bool fresh = *sink != d.message;
            *sink = d.message;
            hits_dirty_ = true;   // tells the panel (hits_changed) to refresh the tooltip
            // The backend's message sink (SES-06): src/debugger holds no core-layer header (debug_qt_free_test QTF-09).
            if (fresh) dbg_.log(client_, jnext::dbg::LogLevel::Warn, "breakpoint condition: " + d.to_string());
        };
        auto c = jnext::script::compile_expr(spec.condition, jnext::script::bp_scope(spec.kind), opts);
        if (!c) {
            if (why) *why = c.errors.empty() ? std::string("the condition is refused") : c.errors[0].to_string();
            return false;
        }
        s.condition = c.predicate;
    }
    if (spec.count_only) {
        s.action  = jnext::dbg::Action::Continue;
        s.handler = [this](const jnext::dbg::Event& e, jnext::dbg::Debugger&) {
            ++hits_[e.id];
            hits_dirty_ = true;
            return jnext::dbg::Action::Continue;
        };
    }
    out = std::move(s);
    return true;
}

void BreakpointModel::add(int type, uint16_t addr, jnext::dbg::MemSpace space) {
    BpSpec s;
    s.kind  = static_cast<BpSpec::Kind>(type);
    s.addr  = addr;
    s.space = space;
    if (type < Execute || type > Nmi) return;
    add(s);
}

void BreakpointModel::add(const BpSpec& in, std::string* error) {
    if (in.kind > BpSpec::Nmi) return;
    BpSpec s = normalised_(in);
    if (find_spec_(s)) return;
    Subscription sub;
    std::shared_ptr<std::string> err;
    if (!build_(s, sub, err, error)) return;
    const auto id = dbg_.subscribe(client_, sub);
    if (id && (s.count_only || !s.condition.empty())) {
        Extra& x    = extras_[id.value];
        x.count_only = s.count_only;
        x.condition  = s.condition;
        x.error      = err;
    }
    publish_();
}

// What add() stores: the space is an Execute's alone, Interrupt / NMI have no address, a range or mask that
// is the default is the default, and a condition is its trimmed text.
BpSpec BreakpointModel::normalised_(BpSpec s) {
    if (s.kind != BpSpec::Execute) s.space = jnext::dbg::MemSpace::cpu();
    if (s.kind == BpSpec::Interrupt || s.kind == BpSpec::Nmi) { s.addr = 0; s.hi.reset(); s.mask.reset(); }
    if (s.hi && *s.hi == s.addr) s.hi.reset();
    if (s.hi) s.space = jnext::dbg::MemSpace::cpu();
    if (s.mask && *s.mask == bp_port_mask_for(s.addr)) s.mask.reset();
    size_t b = 0, e = s.condition.size();
    while (b < e && static_cast<unsigned char>(s.condition[b]) <= ' ') ++b;
    while (e > b && static_cast<unsigned char>(s.condition[e - 1]) <= ' ') --e;
    s.condition = s.condition.substr(b, e - b);
    return s;
}

jnext::dbg::GroupId BreakpointModel::add_source_line(const QString& file_line, bool count_only,
                                                      const std::string& condition, std::string* error) {
    BpSpec like;
    like.count_only = count_only;
    like.condition  = condition;
    like = normalised_(like);
    Subscription sub;
    std::shared_ptr<std::string> err;
    if (!build_(like, sub, err, error)) return jnext::dbg::GROUP_NONE;
    const auto g = dbg_.subscribe_source_line(client_, file_line.toStdString(), sub);
    if (!g) return jnext::dbg::GROUP_NONE;
    group_lines_[g.value] = file_line.toStdString();
    if (count_only || !like.condition.empty())
        for (jnext::dbg::EventId id : group_ids_(g.value)) {
            Extra& x     = extras_[id];
            x.count_only = count_only;
            x.condition  = like.condition;
            x.error      = err;
        }
    publish_();
    return g.value;
}

void BreakpointModel::remove(int type, uint16_t addr, jnext::dbg::MemSpace space) {
    const Member* m = find_own_(type, addr, space);
    if (!m) return;
    dbg_.unsubscribe(client_, m->id);
    publish_();
}

void BreakpointModel::remove(const Row& row) {
    if (row.group != jnext::dbg::GROUP_NONE) dbg_.unsubscribe_group(client_, row.group);
    else                                     dbg_.unsubscribe(client_, row.id);
    publish_();
}

void BreakpointModel::set_enabled(int type, uint16_t addr, bool enabled,
                                  jnext::dbg::MemSpace space, bool count_only) {
    const Member* m = find_own_(type, addr, space, count_only);
    if (!m || m->enabled == enabled) return;
    dbg_.set_enabled(client_, m->id, enabled);
    publish_();
}

void BreakpointModel::set_enabled(const BpSpec& spec, bool enabled) {
    const Member* m = find_spec_(normalised_(spec));
    if (!m || m->enabled == enabled) return;
    dbg_.set_enabled(client_, m->id, enabled);
    publish_();
}

void BreakpointModel::set_enabled(const Row& row, bool enabled) {
    if (row.group != jnext::dbg::GROUP_NONE) dbg_.set_group_enabled(client_, row.group, enabled);
    else                                     dbg_.set_enabled(client_, row.id, enabled);
    publish_();
}

void BreakpointModel::clear_all() {
    std::vector<jnext::dbg::EventId> ids;
    for (const Member& m : members_) ids.push_back(m.id);
    for (jnext::dbg::EventId id : ids) dbg_.unsubscribe(client_, id);
    publish_();
}

std::vector<jnext::script::BpSpec> BreakpointModel::script_specs() const {
    using jnext::script::BpSpec;
    std::vector<BpSpec> out;
    for (const Row& r : rows_) {
        if (!r.own) continue;
        BpSpec s;
        s.kind    = static_cast<BpSpec::Kind>(r.type);
        s.addr    = r.addr;
        s.space   = r.space;
        s.enabled = r.enabled;
        s.count_only = r.count_only;
        if (r.hi != r.addr) s.hi = r.hi;
        if (r.mask != bp_port_mask_for(r.addr)) s.mask = r.mask;
        s.condition = r.condition.toStdString();
        if (r.group != jnext::dbg::GROUP_NONE) {
            const auto line = group_lines_.find(r.group);
            if (line != group_lines_.end()) s.file_line = line->second;
            for (const Member& m : members_)
                if (m.group == r.group) s.locations.push_back({m.space, m.addr});
        }
        out.push_back(std::move(s));
    }
    return out;
}

void BreakpointModel::add_specs(const std::vector<jnext::script::BpSpec>& specs) {
    for (const jnext::script::BpSpec& s : specs) {
        if (s.file_line.empty()) {
            add(s);
            // The same normalisation add() applied finds the breakpoint again, by its full spec.
            set_enabled(s, s.enabled);
            continue;
        }
        // add_source_line() is not idempotent: look for the same places first.
        const Row* same = nullptr;
        for (const Row& r : rows_) {
            if (!r.own || r.group == jnext::dbg::GROUP_NONE || r.count_only != s.count_only ||
                r.condition.toStdString() != normalised_(s).condition)
                continue;
            std::vector<::SourceAddress> places;
            for (const Member& m : members_)
                if (m.group == r.group) places.push_back({m.space, m.addr});
            if (jnext::script::same_locations(places, s.locations)) {
                same = &r;
                break;
            }
        }
        if (same) {
            set_enabled(*same, s.enabled);
            continue;
        }
        const jnext::dbg::GroupId g = add_source_line(QString::fromStdString(s.file_line), s.count_only, s.condition);
        if (g == jnext::dbg::GROUP_NONE || s.enabled) continue;
        for (const Row& r : rows_)
            if (r.group == g) { set_enabled(r, false); break; }
    }
}

bool BreakpointModel::exists(int type, uint16_t addr, jnext::dbg::MemSpace space) const {
    return find_own_(type, addr, space) != nullptr;
}

bool BreakpointModel::pc_exists(uint16_t addr) const {
    return find_own_(Execute, addr) != nullptr;
}

bool BreakpointModel::pc_live(uint16_t addr) const {
    const Member* m = find_own_(Execute, addr);
    return m && m->live;
}

bool BreakpointModel::pc_marked(uint16_t addr, jnext::dbg::MemSpace space) const {
    return find_own_(Execute, addr) || find_own_(Execute, addr, space);
}

bool BreakpointModel::pc_marked_live(uint16_t addr, jnext::dbg::MemSpace space) const {
    const Member* any = find_own_(Execute, addr);
    const Member* on  = find_own_(Execute, addr, space);
    return (any && any->live) || (on && on->live);
}

std::vector<BreakpointModel::PcBreakpoint> BreakpointModel::pc_breakpoints() const {
    std::vector<PcBreakpoint> out;
    for (const Member& m : members_)
        if (m.type == Execute && !m.count_only && plain_(m)) out.push_back(PcBreakpoint{m.addr, m.space, m.live});
    return out;
}

QString BreakpointModel::address_text(uint16_t addr, jnext::dbg::MemSpace space) {
    return address_text(addr, space, jnext::dbg::AddressStyle::Friendly, false);
}

QString BreakpointModel::address_text(uint16_t addr, jnext::dbg::MemSpace space,
                                      jnext::dbg::AddressStyle style, bool next_layout) {
    return QString::asprintf("$%04X", addr) + space_suffix(space, addr, style, next_layout);
}

void BreakpointModel::set_address_style(jnext::dbg::AddressStyle style) {
    if (style_ == style) return;
    style_ = style;
    publish_();
}

bool BreakpointModel::master_enabled() const { return dbg_.master_enabled(); }

void BreakpointModel::set_master_enabled(bool enabled) {
    if (dbg_.master_enabled() == enabled) return;
    dbg_.set_master_enabled(enabled);
    publish_();
}
