#include "debugger/breakpoint_model.h"

#include <algorithm>

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

const char* const kTypeNames[] = {"Execute", "Read", "Write", "Read/Write",
                                  "IO Read", "IO Write"};

// GH #222 — a port is decoded by address-line masking. The Add dialog's rule,
// unchanged from the BreakpointSet it replaces: 00-FF is a LOW-BYTE match (any
// port whose low byte it is — FE catches every ULA access); 0100 and up is that
// exact 16-bit port. So the exact port 0x00xx cannot be named, which no Next
// port needs.
uint16_t port_mask_for(uint16_t addr) { return addr <= 0x00FF ? 0x00FF : 0xFFFF; }

// The subscription one GUI breakpoint is: a single address, no condition, no
// handler, not once, the Stop action (REQ-qt-13d). READ_WRITE is ONE row with
// both access bits (REQ-qt-13c).
Subscription sub_for(int type, uint16_t addr, uint16_t page) {
    Subscription s;
    s.action = jnext::dbg::Action::Stop;
    switch (type) {
        case BreakpointModel::Execute:
            s.kind        = EventKind::Execute;
            s.filter.lo   = s.filter.hi = addr;
            s.filter.page = page;   // CAP-SRC; PAGE_ANY = a logical breakpoint
            // A page here names RAM (a source line's): never match ROM or an
            // overlay that happens to carry the same page number.
            s.filter.page_ram_only = page != jnext::dbg::PAGE_ANY;
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
            s.filter.port_mask  = port_mask_for(addr);
            s.filter.port_value = addr;
            s.access = type == BreakpointModel::IoRead ? Access::Read : Access::Write;
            break;
    }
    return s;
}

// The inverse: which of the six types this listed subscription IS, or -1 when
// it is something the Add dialog cannot express. Only such a subscription can
// be this GUI's own editable row.
int type_of(const SubscriptionInfo& si) {
    const auto& f = si.filter;
    if (si.has_condition || si.has_handler || si.once ||
        si.action != jnext::dbg::Action::Stop ||
        !f.pages.empty() || f.source != jnext::dbg::EventSource::Any)
        return -1;
    // A page qualifier is expressible on an Execute breakpoint only (CAP-SRC).
    if (f.page != jnext::dbg::PAGE_ANY && si.kind != EventKind::Execute) return -1;
    switch (si.kind) {
        case EventKind::Execute:
            return f.lo == f.hi ? BreakpointModel::Execute : -1;
        case EventKind::Mem:
            if (f.lo != f.hi) return -1;
            if (si.access == Access::Read)      return BreakpointModel::Read;
            if (si.access == Access::Write)     return BreakpointModel::Write;
            if (si.access == Access::ReadWrite) return BreakpointModel::ReadWrite;
            return -1;
        case EventKind::Port:
            if (f.port_mask != port_mask_for(f.port_value)) return -1;
            if (si.access == Access::Read)  return BreakpointModel::IoRead;
            if (si.access == Access::Write) return BreakpointModel::IoWrite;
            return -1;
        default:
            return -1;
    }
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
    std::vector<Row>  rows;
    std::vector<Seen> seen;
    for (const SubscriptionInfo& si : dbg_.subscriptions(/*include_transient=*/false)) {
        Row r;
        r.id      = si.id;
        r.type    = type_of(si);
        r.enabled = si.enabled;
        r.live    = si.live;
        r.own     = si.owner == client_ && r.type >= 0;
        const bool port = si.kind == EventKind::Port;
        r.addr = port ? si.filter.port_value : si.filter.lo;
        r.page = si.kind == EventKind::Execute ? si.filter.page : jnext::dbg::PAGE_ANY;
        if (r.type >= 0) {
            r.type_text = QString::fromLatin1(kTypeNames[r.type]);
            r.addr_text = r.page == jnext::dbg::PAGE_ANY
                              ? QString::asprintf("$%04X", r.addr)
                              : QString::asprintf("$%04X @%02X", r.addr, r.page);
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

    rows_        = std::move(rows);
    seen_        = std::move(seen);
    master_seen_ = master;
    if (kinds) emit changed(kinds);
}

void BreakpointModel::sync() {
    if (!dirty_) return;
    dirty_ = false;
    publish_();
}

const BreakpointModel::Row* BreakpointModel::find_own_(int type, uint16_t addr,
                                                       uint16_t page) const {
    for (const Row& r : rows_)
        if (r.own && r.type == type && r.addr == addr && r.page == page) return &r;
    return nullptr;
}

void BreakpointModel::add(int type, uint16_t addr, uint16_t page) {
    if (type < Execute || type > IoWrite) return;
    if (type != Execute) page = jnext::dbg::PAGE_ANY;
    if (find_own_(type, addr, page)) return;
    dbg_.subscribe(client_, sub_for(type, addr, page));
    publish_();
}

void BreakpointModel::remove(int type, uint16_t addr, uint16_t page) {
    const Row* r = find_own_(type, addr, page);
    if (!r) return;
    dbg_.unsubscribe(client_, r->id);
    publish_();
}

void BreakpointModel::set_enabled(int type, uint16_t addr, bool enabled, uint16_t page) {
    const Row* r = find_own_(type, addr, page);
    if (!r || r->enabled == enabled) return;
    dbg_.set_enabled(client_, r->id, enabled);
    publish_();
}

void BreakpointModel::clear_all() {
    std::vector<jnext::dbg::EventId> ids;
    for (const Row& r : rows_)
        if (r.own) ids.push_back(r.id);
    for (jnext::dbg::EventId id : ids) dbg_.unsubscribe(client_, id);
    publish_();
}

bool BreakpointModel::exists(int type, uint16_t addr, uint16_t page) const {
    return find_own_(type, addr, page) != nullptr;
}

bool BreakpointModel::pc_exists(uint16_t addr) const {
    return find_own_(Execute, addr) != nullptr;
}

bool BreakpointModel::pc_live(uint16_t addr) const {
    const Row* r = find_own_(Execute, addr);
    return r && r->live;
}

bool BreakpointModel::pc_marked(uint16_t addr, uint16_t page) const {
    return find_own_(Execute, addr) || find_own_(Execute, addr, page);
}

bool BreakpointModel::pc_marked_live(uint16_t addr, uint16_t page) const {
    const Row* any = find_own_(Execute, addr);
    const Row* on  = find_own_(Execute, addr, page);
    return (any && any->live) || (on && on->live);
}

bool BreakpointModel::master_enabled() const { return dbg_.master_enabled(); }

void BreakpointModel::set_master_enabled(bool enabled) {
    if (dbg_.master_enabled() == enabled) return;
    dbg_.set_master_enabled(enabled);
    publish_();
}
