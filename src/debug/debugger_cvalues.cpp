// CAP-SRC (GH #434 stage 3, epic #353): the C variables of a frame, read through the page-aware identity.
// The pure rules (formatting, the path grammar, which locals are visible) are c_value.{h,cpp}; this file only finds
// the object a name means, where it lives, and reads it.
//
//  * a global, a file static and a static local live in the page their map symbol was placed in (`CStorage::at`),
//    read from THAT page whatever the MMU maps now (a note says when it is not mapped);
//  * a stack variable lives in the CPU view at the frame's base `fp` + its offset, if it is allocated;
//  * a register local is frame 0's register, and "not available" in an older frame;
//  * what a pointer points at is read through the CPU view (C semantics: the program dereferences its current mapping).
//
// None of this writes state: the readers run `recheck()` (through `variables()` and `c_frames()`) like every CAP-SRC reader,
// and bump nothing themselves.

#include "debug/debugger.h"
#include "debug/variable_store.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace jnext {
namespace dbg {

namespace {

using Row = Debugger::CValueRow;
using Frame = Debugger::CFrame;

struct Env {
    const Debugger& d;
    std::shared_ptr<const VariableStore> store;
    std::vector<Frame> frames;
    std::map<std::string, std::string> file_of;   // CDB module -> source file
};

std::string hex4(unsigned v)
{
    static const char* digits = "0123456789ABCDEF";
    std::string s = "0000";
    for (int i = 3; i >= 0; --i, v >>= 4) s[static_cast<size_t>(i)] = digits[v & 15];
    return s;
}

std::string space_name(const MemSpace& s)
{
    switch (s.kind) {
    case MemSpace::Kind::Page: return "page " + std::to_string(s.index);
    case MemSpace::Kind::DivMmcRam: return "DivMMC RAM " + std::to_string(s.index);
    default: return "space " + std::to_string(static_cast<unsigned>(s.kind)) + ":" + std::to_string(s.index);
    }
}

std::string with_c(const std::string& f)
{
    return f.find('.') == std::string::npos ? f + ".c" : f;
}

std::string module_name_of(const std::string& qualifier)
{
    std::string m;
    for (const char c : qualifier) m += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return m;
}

void build_files(Env& e)
{
    for (const CVariable& v : e.store->variables())
        if ((v.record.scope == CdbScope::File || v.record.scope == CdbScope::Local) && !v.record.scope_name.empty())
            e.file_of.emplace(v.module, with_c(v.record.scope_name));
}

std::string file_of(const Env& e, const std::string& module)
{
    const auto it = e.file_of.find(module);
    if (it != e.file_of.end()) return it->second;
    if (module.size() > 2 && module.compare(module.size() - 2, 2, "_c") == 0) return module.substr(0, module.size() - 2) + ".c";
    return module;
}

bool file_matches(const Env& e, const std::string& module, const std::string& qualifier)
{
    return module == module_name_of(qualifier) || file_of(e, module) == with_c(qualifier);
}

Env make_env(const Debugger& d)
{
    Env e{d, d.variables(), {}, {}};
    if (e.store) {
        e.frames = d.c_frames();
        build_files(e);
    }
    return e;
}

CReader cpu_reader(const Debugger& d)
{
    return [&d](uint32_t a, size_t n, uint8_t* out) {
        const auto r = d.peek(MemSpace::cpu(), a, n, out);
        return r.status == Result::Ok && r.value == n;
    };
}

// A root object's own bytes: the page it was placed in. A value that would cross the end of its 8 KB page is refused.
CReader space_reader(const Debugger& d, MemSpace space)
{
    return [&d, space](uint32_t a, size_t n, uint8_t* out) {
        const uint32_t off = space_offset(space, static_cast<uint16_t>(a));
        if (off + n > space_size(space.kind)) return false;
        const auto r = d.peek(space, off, n, out);
        return r.status == Result::Ok && r.value == n;
    };
}

std::function<std::string(uint16_t)> name_lookup(const Env& e)
{
    return [&e](uint16_t addr) -> std::string {
        const MemSpace now = e.d.source_page(addr);
        for (const CVariable& v : e.store->variables())
            if (v.record.hidden == CdbHidden::None && v.storage.resolved && v.storage.at.address == addr && v.storage.at.space == now)
                return " <" + v.c_name + ">";
        for (const CFunction& f : e.store->functions())
            if (f.entry.resolved && f.entry.at.address == addr && f.entry.at.space == now) return " <" + f.name + ">";
        return "";
    };
}

bool is_aggregate(const CdbType& t)
{
    return !t.chain.empty() && (t.chain[0].kind == CdbTypeKind::Struct || t.chain[0].kind == CdbTypeKind::Array);
}

Debugger::CValueResult refused(const std::string& why)
{
    Debugger::CValueResult r;
    r.why = why;
    return r;
}

// The format context of variable `v`: how its type is looked up and what its pointers' targets are read through.
CFormatCtx var_ctx(const Env& e, const CVariable& v)
{
    CFormatCtx ctx;
    ctx.store = e.store.get();
    ctx.scope_name = v.record.scope_name;
    ctx.module = v.module;
    ctx.read = cpu_reader(e.d);
    ctx.name_at = name_lookup(e);
    return ctx;
}

// z88dk#3233: 80cc may leave a local in a register, and records a register-passed parameter at its nominal slot, which can be
// another variable's. A stack variable of an 80cc function is therefore shown with this note, whatever its offset.
const char* const k80ccSlot =
    "80cc may not keep it in the frame slot its record names: the value shown can be stale or another variable's (z88dk#3233)";

// OD-A (GH #438): with no listing beside the map nothing flags a function `__z88dk_fastcall`, but its ENTRY says so: sccz80 calls
// `l_debug_push_frame` and then pushes the register parameter (`call l_debug_push_frame; push hl`, or `push de` first for a
// 32-bit one). The bytes are read from the function's own page (the entry's page-aware key), whatever is mapped now.
bool prologue_is_fastcall(const Env& e, int function)
{
    const CFunction& f = e.store->functions()[static_cast<size_t>(function)];
    const CStorage& pf = e.store->helpers().push_frame;
    if (f.conv != CConv::Unlisted || !f.entry.resolved || !pf.resolved) return false;
    uint8_t b[5] = {0, 0, 0, 0, 0};
    const MemSpace space = f.entry.at.space;
    const uint32_t off = space_offset(space, f.entry.at.address);
    if (off + 5 > space_size(space.kind)) return false;
    const auto r = e.d.peek(space, off, 5, b);
    if (r.status != Result::Ok || r.value != 5) return false;
    const bool call = b[0] == 0xCD && static_cast<uint16_t>(b[1] | b[2] << 8) == pf.at.address;
    return call && (b[3] == 0xE5 || (b[3] == 0xD5 && b[4] == 0xE5));
}

// Read variable `v` (and the path's steps) in frame `k`.
Debugger::CValueResult eval_var(const Env& e, size_t k, const CVariable& v, bool deref, const std::vector<CPathStep>& steps)
{
    using CResult = Debugger::CValueResult;
    CResult out;
    const CdbVar& cv = v.record.var;
    out.type = c_type_text(cv.type);
    out.aggregate = is_aggregate(cv.type);

    CFormatCtx ctx = var_ctx(e, v);

    // OD-14 / z88dk U1 (GH #438: per FUNCTION): in a fastcall function whose parameters are all recorded at nominal slots,
    // none of them is there.
    if (v.function >= 0 && cv.space == 'B' && cv.level == 0 && cv.stack_offset() >= 0 &&
        c_fastcall_records_nominal(*e.store, v.function, prologue_is_fastcall(e, v.function))) {
        CResult r = refused(kCU1Text);
        r.type = out.type;
        r.aggregate = out.aggregate;
        return r;
    }

    CLoc root;
    root.type = cv.type;
    if (cv.space == 'E') {
        if (v.record.hidden == CdbHidden::EmptyType || !v.storage.resolved) {
            CResult r = refused(v.storage.resolved ? "has no storage" : v.storage.why);
            r.type = out.type;
            r.aggregate = out.aggregate;
            return r;
        }
        const MemSpace space = v.storage.at.space;
        root.addr = v.storage.at.address;
        root.rooted = true;
        ctx.read_root = space_reader(e.d, space);
        out.where.kind = Debugger::CWhere::Kind::Page;
        out.where.source = v.storage.at;
        if (!(e.d.source_page(root.addr) == space))
            out.notes.push_back(space_name(space) + " is not mapped at $" + hex4(root.addr) + " now");
    } else if (cv.space == 'B') {
        if (k >= e.frames.size()) return refused("no frame " + std::to_string(k));
        const Frame& fr = e.frames[k];
        if (fr.how == Frame::Base::Unknown) return refused("the frame base is not known: " + fr.note);
        const int addr = static_cast<int>(fr.fp) + cv.stack_offset();
        if (cv.stack_offset() < 0 && addr < static_cast<int>(fr.floor)) return refused("not allocated");
        if (v.function >= 0 && e.store->functions()[static_cast<size_t>(v.function)].compiler == CCompiler::Cc80)
            out.notes.push_back(k80ccSlot);
        root.addr = static_cast<uint32_t>(addr) & 0xFFFF;
        out.where.kind = Debugger::CWhere::Kind::Cpu;
        out.where.cpu = static_cast<uint16_t>(root.addr);
    } else if (cv.space == 'R') {
        CRegs regs;
        const CRegs* rp = nullptr;
        if (k == 0) {
            const Z80Registers z = e.d.registers();
            regs.af = z.AF;
            regs.bc = z.BC;
            regs.de = z.DE;
            regs.hl = z.HL;
            regs.ix = z.IX;
            regs.iy = z.IY;
            rp = &regs;
        }
        const CRegRead rr = c_read_register_local(cv, rp);
        if (!rr.ok) return refused(rr.why);
        // a pointer held in a register can be followed (`*p`, `p->x`, `p[i]`): its VALUE is the register's; a member or an element of a
        // register-held aggregate has no address
        const bool follows = !cv.type.chain.empty() && cv.type.chain[0].kind == CdbTypeKind::Pointer && rr.bytes.size() >= 2 &&
                             (deref || (!steps.empty() && steps[0].kind != CPathStep::Kind::Dot));
        if (follows) {
            root.ptr_value = static_cast<uint16_t>(rr.bytes[0] | rr.bytes[1] << 8);
            out.notes.push_back(rr.note);
        } else {
            if (deref || !steps.empty()) return refused("a register value has no address");
            // no address, but the type (the int32 atom of GH #436 decides on it) and a pointer's VALUE, which c_children() follows
            out.loc.type = cv.type;
            if (!cv.type.chain.empty() && cv.type.chain[0].kind == CdbTypeKind::Pointer && rr.bytes.size() >= 2)
                out.loc.ptr_value = static_cast<uint16_t>(rr.bytes[0] | rr.bytes[1] << 8);
            const CFormatted f = c_format_value(ctx, cv.type, rr.bytes.data(), rr.bytes.size());
            if (!f.ok) return refused(f.why);
            out.ok = true;
            out.value = f.text;
            out.notes = f.notes;
            out.notes.push_back(rr.note);
            out.where.kind = Debugger::CWhere::Kind::Register;
            out.where.regs = rr.note.substr(3, rr.note.find(';') - 3);
            out.integer = c_integer_value(cv.type, rr.bytes.data(), rr.bytes.size());
            out.real = c_float_value(cv.type, rr.bytes.data(), rr.bytes.size());
            return out;
        }
    } else {
        return refused("no location is recorded for it");
    }
    root.end = 0;

    const CEval ev = c_eval_path(ctx, root, deref, steps);
    if (!ev.ok) {
        CResult r = refused(ev.why);
        r.type = out.type;
        r.aggregate = out.aggregate;
        return r;
    }
    std::vector<uint8_t> bytes;
    const CFormatted f = c_read_loc(ctx, ev.loc, &bytes);
    out.type = c_type_text(ev.loc.type);
    out.aggregate = is_aggregate(ev.loc.type);
    out.loc = ev.loc;
    if (ev.loc.rooted) {
        out.where.source = {out.where.source.space, static_cast<uint16_t>(ev.loc.addr)};
    } else {
        out.where.kind = Debugger::CWhere::Kind::Cpu;
        out.where.cpu = static_cast<uint16_t>(ev.loc.addr);
        out.notes.erase(std::remove_if(out.notes.begin(), out.notes.end(),
                                       [](const std::string& n) { return n.find("is not mapped at") != std::string::npos; }),
                        out.notes.end());
    }
    if (!f.ok) {
        out.why = f.why;
        if (ev.loc.rooted && cv.space == 'E' && ev.loc.type.size > 0 &&
            space_offset(v.storage.at.space, static_cast<uint16_t>(ev.loc.addr)) + static_cast<uint32_t>(ev.loc.type.size) > space_size(v.storage.at.space.kind))
            out.why = "the value crosses a page boundary";
        return out;
    }
    out.ok = true;
    out.value = f.text;
    for (const std::string& n : f.notes) out.notes.push_back(n);
    if (!ev.loc.note.empty() && std::find(out.notes.begin(), out.notes.end(), ev.loc.note) == out.notes.end())
        out.notes.push_back(ev.loc.note);
    out.integer = c_integer_value(ev.loc.type, bytes.data(), bytes.size());
    out.real = c_float_value(ev.loc.type, bytes.data(), bytes.size());
    return out;
}

struct Found {
    const CVariable* v = nullptr;
    Row::Kind kind = Row::Kind::Local;
    std::string why;
    bool scope_unknown = false;   // shown because the block at the PC is not known (80cc): a note for a read, a refusal for a write
};

std::string where_text(const Env& e, size_t k)
{
    if (k < e.frames.size() && e.frames[k].line) return e.frames[k].line->file + ":" + std::to_string(e.frames[k].line->line);
    return "this point";
}

Row::Kind kind_of_local(CVarKind k)
{
    return k == CVarKind::Param ? Row::Kind::Param : k == CVarKind::StaticLocal ? Row::Kind::StaticLocal : Row::Kind::Local;
}

// The visible local (parameter, local, static local) of function `f` at frame `k` named `name`.
Found find_local(const Env& e, size_t k, int f, const std::string& name)
{
    Found out;
    const auto loc = k < e.frames.size() ? e.store->locate(e.frames[k].pc) : std::nullopt;
    const CScope* scope = loc ? e.store->scope_at(f, loc->offset) : nullptr;
    for (const CLocalRef& r : c_visible_locals(*e.store, f, scope))
        if (r.var->c_name == name) {
            out.v = r.var;
            out.kind = kind_of_local(r.kind);
            out.scope_unknown = r.scope_unknown;
            return out;
        }
    for (const CVariable& v : e.store->variables())
        if (v.function == f && v.c_name == name && v.record.hidden == CdbHidden::None) {
            out.why = "out of scope at " + where_text(e, k);
            return out;
        }
    return out;
}

Found find_var(const Env& e, size_t k, const CPath& p)
{
    Found out;
    const auto& vars = e.store->variables();
    const int cur = k < e.frames.size() ? e.frames[k].function : -1;
    const auto module_of_cur = [&] { return cur >= 0 ? e.store->functions()[static_cast<size_t>(cur)].module : std::string(); };
    const auto visible = [](const CVariable& v) { return v.record.hidden == CdbHidden::None; };

    if (p.primary == CPath::Primary::Function) {
        int f = -1, n = 0;
        for (size_t i = 0; i < e.store->functions().size(); ++i)
            if (e.store->functions()[i].name == p.qualifier && (p.file.empty() || file_matches(e, e.store->functions()[i].module, p.file))) {
                if (cur >= 0 && static_cast<int>(i) == cur) { f = static_cast<int>(i); n = 1; break; }
                f = static_cast<int>(i);
                ++n;
            }
        if (n == 0) { out.why = "no function '" + p.qualifier + "'" + (p.file.empty() ? std::string() : " in " + p.file); return out; }
        if (n > 1) { out.why = "function '" + p.qualifier + "' is ambiguous: name its file"; return out; }
        for (const CVariable& v : vars)
            if (visible(v) && v.function == f && v.c_name == p.name && v.record.var.space == 'E') {
                out.v = &v;
                out.kind = Row::Kind::StaticLocal;
                return out;
            }
        if (cur == f) {
            out = find_local(e, k, f, p.name);
            if (!out.v && out.why.empty()) out.why = "no variable '" + p.name + "' in " + p.qualifier;
            return out;
        }
        for (const CVariable& v : vars)
            if (visible(v) && v.function == f && v.c_name == p.name) {
                out.why = "'" + p.name + "' is a local of " + p.qualifier + ", which frame " + std::to_string(k) + " is not";
                return out;
            }
        out.why = "no variable '" + p.name + "' in " + p.qualifier;
        return out;
    }
    if (p.primary == CPath::Primary::File) {
        const CVariable* found = nullptr;
        for (const CVariable& v : vars) {
            if (!visible(v) || v.function >= 0 || v.c_name != p.name) continue;
            if (v.record.scope != CdbScope::File && v.record.scope != CdbScope::Global) continue;
            if (!file_matches(e, v.module, p.qualifier)) continue;
            if (found) { out.why = "'" + p.name + "' is defined more than once in " + p.qualifier; return out; }
            found = &v;
        }
        if (!found) { out.why = "no variable '" + p.name + "' in file " + p.qualifier; return out; }
        out.v = found;
        out.kind = found->record.scope == CdbScope::Global ? Row::Kind::Global : Row::Kind::FileStatic;
        return out;
    }
    // a plain name: the frame's locals, then its function's static locals, the file's statics, the globals
    if (cur >= 0) {
        out = find_local(e, k, cur, p.name);
        if (out.v || !out.why.empty()) return out;
        const std::string module = module_of_cur();
        for (const CVariable& v : vars)
            if (visible(v) && v.function < 0 && v.record.scope == CdbScope::File && v.module == module && v.c_name == p.name &&
                v.record.var.space == 'E') {
                out.v = &v;
                out.kind = Row::Kind::FileStatic;
                return out;
            }
    }
    for (const CVariable& v : vars)
        if (visible(v) && v.function < 0 && v.record.scope == CdbScope::Global && v.c_name == p.name) {
            out.v = &v;
            out.kind = Row::Kind::Global;
            return out;
        }
    out.why = "unknown name '" + p.name + "'";
    return out;
}

Row to_row(const Env& e, size_t k, const CVariable& v, Row::Kind kind, const std::string& path)
{
    Row r;
    r.name = v.c_name;
    r.path = path;
    r.kind = kind;
    r.type = c_type_text(v.record.var.type);
    r.aggregate = is_aggregate(v.record.var.type);
    r.file = file_of(e, v.module);
    const Debugger::CValueResult res = eval_var(e, k, v, false, {});
    r.where = res.where;
    r.ok = res.ok;
    r.value = res.value;
    r.why = res.why;
    r.notes = res.notes;
    if (res.ok) r.aggregate = res.aggregate;
    return r;
}

}  // namespace

Debugger::CVariablesStatus Debugger::c_variables_status() const
{
    CVariablesStatus st;
    const SourceMapStatus ms = source_map_status();
    if (ms.state == SourceMapStatus::State::None) {
        st.why = "no source map is attached";
        return st;
    }
    if (ms.state == SourceMapStatus::State::Stale) {
        st.why = "the source map is stale: " + ms.reason;
        return st;
    }
    if (source_map().format() == "SLD") {
        st.why = "an SLD source map carries no C variables (they come from a z88dk -debug map)";
        return st;
    }
    const auto vs = variables();
    if (!vs) {
        st.why = "no source map is attached";
        return st;
    }
    if (vs->functions().empty() && vs->variables().empty()) {
        if (!vs->refused().empty())
            st.why = "all " + std::to_string(vs->refused().size()) + " C variable records were refused: " + vs->refused().front();
        else if (!vs->no_record_facts().c_line_records)
            st.why = "the map has no C debug records: build with zcc -debug (z88dk 2.4's zsdcc fails with -debug, z88dk#3172)";
        else if (vs->no_record_facts().zsdcc_form)
            st.why = "zsdcc wrote no C variable records for this build (zcc -O0 writes none)";
        else
            st.why = "the map has line records but no __CDBINFO__ records";
        return st;
    }
    st.available = true;
    return st;
}

Debugger::CLocalsResult Debugger::c_locals(size_t frame) const
{
    CLocalsResult out;
    const Env e = make_env(*this);
    if (!e.store) {
        out.why = c_variables_status().why;
        return out;
    }
    if (frame >= e.frames.size()) {
        out.why = "no frame " + std::to_string(frame) + " (there are " + std::to_string(e.frames.size()) + ")";
        return out;
    }
    const Frame& fr = e.frames[frame];
    if (fr.function < 0) {
        out.why = "frame " + std::to_string(frame) + " is not in a C function" + (fr.note.empty() ? std::string() : " (" + fr.note + ")");
        return out;
    }
    if (fr.how == Frame::Base::Unknown) {
        out.why = "the base of frame " + std::to_string(frame) + " is not known: " + fr.note;
        return out;
    }
    const auto loc = e.store->locate(fr.pc);
    const CScope* scope = loc ? e.store->scope_at(fr.function, loc->offset) : nullptr;
    for (const CLocalRef& r : c_visible_locals(*e.store, fr.function, scope)) {
        Row row = to_row(e, frame, *r.var, kind_of_local(r.kind), r.var->c_name);
        if (r.scope_unknown) row.notes.push_back("scope unknown: shown whatever block the PC is in");
        out.rows.push_back(row);
    }
    out.ok = true;
    return out;
}

std::vector<Debugger::CValueRow> Debugger::c_globals() const
{
    std::vector<CValueRow> rows;
    const Env e = make_env(*this);
    if (!e.store) return rows;
    for (const CVariable& v : e.store->variables()) {
        if (v.record.hidden != CdbHidden::None || v.record.var.space != 'E') continue;
        const bool static_local = v.function >= 0;
        if (!static_local && v.record.scope != CdbScope::Global && v.record.scope != CdbScope::File) continue;
        const std::string file = file_of(e, v.module);
        std::string path;
        Row::Kind kind;
        if (static_local) {
            const std::string& fname = e.store->functions()[static_cast<size_t>(v.function)].name;
            const bool unique = std::count_if(e.store->functions().begin(), e.store->functions().end(),
                                              [&](const CFunction& f) { return f.name == fname; }) == 1;
            path = (unique ? std::string() : file + "::") + fname + "::" + v.c_name;   // the file only when the function name repeats
            kind = Row::Kind::StaticLocal;
        } else {
            path = file + "::" + v.c_name;
            kind = v.record.scope == CdbScope::Global ? Row::Kind::Global : Row::Kind::FileStatic;
        }
        rows.push_back(to_row(e, 0, v, kind, path));
    }
    std::sort(rows.begin(), rows.end(), [](const CValueRow& a, const CValueRow& b) {
        return a.file != b.file ? a.file < b.file : a.name != b.name ? a.name < b.name : a.path < b.path;
    });
    return rows;
}

Debugger::CValueResult Debugger::read_c_value(size_t frame, std::string_view path) const
{
    const Env e = make_env(*this);
    if (!e.store) return refused(c_variables_status().why);
    const CPath p = c_parse_path(path);
    if (!p.ok) return refused(p.why);
    if (frame >= e.frames.size()) return refused("no frame " + std::to_string(frame) + " (there are " + std::to_string(e.frames.size()) + ")");
    const Found f = find_var(e, frame, p);
    if (!f.v) return refused(f.why);
    return eval_var(e, frame, *f.v, p.deref, p.steps);
}

// `in BC; may be stale ...` -> `BC`: the registers a register local (or the pointer a path followed from one) lives in.
static std::string registers_of(const Debugger::CValueResult& r)
{
    if (r.where.kind == Debugger::CWhere::Kind::Register) return r.where.regs;
    for (const std::string& n : r.notes)
        if (n.compare(0, 3, "in ") == 0) return n.substr(3, n.find(';') == std::string::npos ? std::string::npos : n.find(';') - 3);
    return "registers";
}

Debugger::CWriteResult Debugger::write_c_value(ClientId by, size_t frame, std::string_view path, const CNewValue& value)
{
    CWriteResult out;
    const auto refuse = [&out](const std::string& why) {
        out.status = Result::RefusedUnavailable;
        out.why = why;
        return out;
    };
    const Env e = make_env(*this);
    if (!e.store) return refuse(c_variables_status().why);
    const CPath p = c_parse_path(path);
    if (!p.ok) return refuse(p.why);
    if (frame >= e.frames.size())
        return refuse("no frame " + std::to_string(frame) + " (there are " + std::to_string(e.frames.size()) + ")");
    const Found f = find_var(e, frame, p);
    if (!f.v) return refuse(f.why);
    if (f.scope_unknown)
        return refuse("scope unknown: the block at the PC is not known (80cc), so the slot may hold another variable");
    // the place is the read's: a write needs a successful read of the same path
    const CValueResult r = eval_var(e, frame, *f.v, p.deref, p.steps);
    out.where = r.where;
    if (!r.ok) return refuse(r.why);
    out.type = r.type;
    out.old_value = r.value;
    out.notes = r.notes;
    if (f.v->record.var.space == 'R')
        return refuse("a register variable cannot be written: its debug record gives no live range (in " + registers_of(r) + ")");
    if (r.aggregate) return refuse(r.type + " is an aggregate: write a member or an element");
    if (!r.loc.note.empty()) return refuse(r.loc.note);
    if (r.where.kind != CWhere::Kind::Page && r.where.kind != CWhere::Kind::Cpu) return refuse("the object has no address");

    const MemSpace space = r.where.kind == CWhere::Kind::Page ? r.where.source.space : MemSpace::cpu();
    const uint32_t at = r.where.kind == CWhere::Kind::Page ? space_offset(space, r.where.source.address) : r.where.cpu;
    const size_t n = static_cast<size_t>(std::max(1, r.loc.type.size));
    std::vector<uint8_t> cur(n, 0);
    const auto pk = peek(space, at, n, cur.data());
    if (pk.status != Result::Ok || pk.value != n) return refuse("the object cannot be read back for the write");
    const CEncoded enc = c_encode_value(r.loc.type, value, cur.data(), cur.size());
    if (!enc.ok) return refuse(enc.why);

    const auto w = poke(by, space, at, enc.bytes.size(), enc.bytes.data());   // the ONE poke: one MUTATE line and one revision bump when something lands (all of it, or the bytes of a partial ROM landing)
    out.landed = w.value;
    out.status = w.status;
    if (w.status == Result::RefusedReadOnly) {
        out.why = std::to_string(w.value) + " of " + std::to_string(enc.bytes.size()) + " bytes landed (the rest is read-only)";
        return out;
    }
    if (w.status != Result::Ok) {
        out.why = space.kind == MemSpace::Kind::DivMmcRam
                      ? "DivMMC RAM bank " + std::to_string(space.index) + " cannot be written by the debugger (unsupported)"
                      : std::string("the write was refused (") + result_name(w.status) + ")";
        return out;
    }
    const Env e2 = make_env(*this);
    if (e2.store) {
        const CValueResult back = eval_var(e2, frame, *f.v, p.deref, p.steps);
        if (back.ok) {
            out.value = back.value;
            out.where = back.where;
        }
    }
    return out;
}

Debugger::CChildRows Debugger::c_children(size_t frame, std::string_view path, size_t first, size_t max) const
{
    CChildRows out;
    const Env e = make_env(*this);
    if (!e.store) {
        out.why = c_variables_status().why;
        return out;
    }
    const CPath p = c_parse_path(path);
    if (!p.ok) {
        out.why = p.why;
        return out;
    }
    if (frame >= e.frames.size()) {
        out.why = "no frame " + std::to_string(frame) + " (there are " + std::to_string(e.frames.size()) + ")";
        return out;
    }
    const Found f = find_var(e, frame, p);
    if (!f.v) {
        out.why = f.why;
        return out;
    }
    const CValueResult self = eval_var(e, frame, *f.v, p.deref, p.steps);
    if (!self.ok) {
        out.why = self.why;
        return out;
    }
    if (self.where.kind == CWhere::Kind::Register && !self.loc.ptr_value) {
        out.ok = !self.aggregate;   // a register scalar is a leaf; an aggregate in one has no address to walk
        if (!out.ok) out.why = "a register value has no address";
        return out;
    }
    CFormatCtx ctx = var_ctx(e, *f.v);
    if (self.where.kind == CWhere::Kind::Page) ctx.read_root = space_reader(e.d, self.where.source.space);
    const CChildren ch = jnext::dbg::c_children(ctx, self.loc, first, max);
    if (!ch.ok) {
        out.why = ch.why;
        return out;
    }
    out.ok = true;
    out.total = ch.total;
    // `*E` is `E[0]`: the grammar applies a leading `*` AFTER the postfix steps, so `*p` + `.x` would read as `*(p.x)`. `self.loc` is the
    // pointee already, so its members are `.x` (or `->x` if it is itself a pointer) on `E[0]`.
    std::string base(path);
    if (p.deref) {
        const size_t star = base.find('*');
        base = base.substr(star + 1);
        const size_t b = base.find_first_not_of(" \t");
        base = (b == std::string::npos ? std::string() : base.substr(b)) + "[0]";
    }
    for (const CChild& c : ch.items) {
        const std::string child_path = base + c.suffix;
        const CPath cp = c_parse_path(child_path);
        CValueResult r = cp.ok ? eval_var(e, frame, *f.v, cp.deref, cp.steps) : refused(cp.why);
        Row row;
        row.name = c.label;
        row.path = child_path;
        row.file = file_of(e, f.v->module);
        row.type = r.type.empty() ? c_type_text(c.loc.type) : r.type;
        row.kind = f.kind;
        row.where = r.where;
        row.ok = r.ok;
        row.value = r.value;
        row.why = r.why;
        row.aggregate = r.ok ? r.aggregate : is_aggregate(c.loc.type);
        row.notes = r.notes;
        out.rows.push_back(row);
    }
    return out;
}

}  // namespace dbg
}  // namespace jnext
