#include "remote/dzrp/dzrp_target.h"

#include <algorithm>
#include <set>

#include "core/log.h"
#include "debug/inspect.h"
#include "remote/dzrp/dzrp_registers.h"
#include "video/sprites.h"

namespace jnext {
namespace remote {
namespace dzrp {

using jnext::dbg::Access;
using jnext::dbg::Capability;
using jnext::dbg::ConstBytes;
using jnext::dbg::Expected;
using jnext::dbg::make_ok;
using jnext::dbg::make_refused;
using jnext::dbg::MemSpace;
using jnext::dbg::PAGE_ANY;
using jnext::dbg::RegId;
using jnext::dbg::RegisterValidity;
using jnext::dbg::Result;
using jnext::dbg::ServiceStep;
using jnext::dbg::SlotInfo;
using jnext::dbg::TargetStop;

namespace {

constexpr std::size_t kPageBytes = 0x2000;
constexpr std::size_t kLineBytes = 256;
constexpr std::size_t kRegisterReply = 37;   // 12 words, R, I, IM, reserved, Nslots, 8 slots

struct CapReq {
    Capability                capability;
    std::vector<std::uint8_t> commands;
};

/// What each capability needs of the remote: every command must be in its set. The two
/// "while running" capabilities are a property of the jnext profile, not of a command.
const std::vector<CapReq>& capability_table() {
    static const std::vector<CapReq> t = {
        {Capability::Run, {CMD_CONTINUE}},
        {Capability::Pause, {CMD_PAUSE}},
        {Capability::ExecuteBreakpoints, {CMD_ADD_BREAKPOINT, CMD_REMOVE_BREAKPOINT}},
        {Capability::Watchpoints, {CMD_ADD_WATCHPOINT, CMD_REMOVE_WATCHPOINT}},
        {Capability::CommandsWhileRunning, {}},
        {Capability::MemoryWhileRunning, {}},
        {Capability::RegisterWrite, {CMD_SET_REGISTER}},
        {Capability::Memory, {CMD_READ_MEM, CMD_WRITE_MEM}},
        {Capability::PhysicalMemory, {CMD_READ_BANK_MEM, CMD_WRITE_BANK_MEM}},
        {Capability::MmuSlots, {CMD_GET_REGISTERS, CMD_SET_SLOT}},
        {Capability::NextRegs, {CMD_GET_TBBLUE_REG}},
        {Capability::Ports, {CMD_READ_PORT, CMD_WRITE_PORT}},
        {Capability::SpriteData, {CMD_GET_SPRITES, CMD_GET_SPRITE_PATTERNS}},
        {Capability::SpritePaletteClip,
         {CMD_GET_SPRITES_PALETTE, CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL}},
    };
    return t;
}

void put16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}

/// Runs of consecutive line numbers, each at most 128 lines (a block size is 16 bits).
std::vector<std::pair<int, int>> runs_of(const std::vector<int>& lines) {
    std::vector<std::pair<int, int>> out;   // first line, count
    for (int l : lines) {
        if (!out.empty() && out.back().first + out.back().second == l && out.back().second < 128)
            ++out.back().second;
        else
            out.push_back({l, 1});
    }
    return out;
}

std::string version_string(const std::uint8_t v[3]) {
    return std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]);
}

}  // namespace

// ── lifetime, identity ──────────────────────────────────────────────────────

DzrpTarget::DzrpTarget(std::unique_ptr<DzrpClient> client) : cl_(std::move(client)) {
    info_.kind    = jnext::dbg::TargetKind::Remote;
    info_.name    = cl_->remote().name;
    info_.version = version_string(cl_->remote().version);
    info_.link    = cl_->link().describe();
    if (cl_->state() != SessionState::Ready) {
        lost_        = true;
        lost_reason_ = cl_->error().empty() ? "the session is not ready" : cl_->error();
    }
    compute_capabilities();
}

DzrpTarget::~DzrpTarget() = default;   // ~DzrpClient writes CMD_CLOSE

bool DzrpTarget::connected() const { return !lost_ && cl_->state() == SessionState::Ready; }

bool DzrpTarget::usable() const { return !lost_ && cl_->profile() == Profile::Jnext; }

std::string DzrpTarget::why_unavailable(Capability c) const {
    const std::size_t i = static_cast<std::size_t>(c);
    if (i >= why_.size() || caps_.has(c)) return std::string();
    return why_[i];
}

void DzrpTarget::compute_capabilities() const {
    caps_     = jnext::dbg::CapabilitySet{};
    validity_ = RegisterValidity{};
    for (auto& w : why_) w.clear();
    std::string reason;
    if (lost_)
        reason = "link lost: " + lost_reason_;
    else if (cl_->profile() != Profile::Jnext)
        reason = "this remote's profile is not served yet";
    for (const CapReq& r : capability_table()) {
        std::string why = reason;
        if (why.empty()) {
            for (std::uint8_t id : r.commands)
                if (!cl_->in_set(id)) {
                    why = "the remote does not serve " + command_name(id);
                    break;
                }
        }
        if (why.empty()) caps_.set(r.capability);
        else why_[static_cast<std::size_t>(r.capability)] = why;
    }
    if (reason.empty()) {
        validity_.set(RegisterValidity::Field::Core);
        validity_.set(RegisterValidity::Field::IM);
    }
}

void DzrpTarget::lose(const std::string& why) const {
    if (lost_) return;
    lost_        = true;
    lost_reason_ = why;
    Log::debugger()->error("dzrp target: lost: {}", why);
    drop_cache();
    compute_capabilities();
}

bool DzrpTarget::good(const Reply& r) const {
    if (r.status == Status::Ok) return true;
    const SessionState st = cl_->state();
    if (st == SessionState::Failed || st == SessionState::Closed)
        lose(cl_->error().empty() ? std::string("the session ended") : cl_->error());
    return false;
}

bool DzrpTarget::length_is(const Reply& r, std::size_t n) const {
    if (r.payload.size() == n) return true;
    lose("protocol error: " + command_name(r.id) + " reply of " + std::to_string(r.payload.size()) +
         " bytes, expected " + std::to_string(n));
    return false;
}

Result DzrpTarget::submit_ack(std::uint8_t id, Bytes payload) {
    const Reply r = cl_->request(id, std::move(payload));
    return good(r) ? Result::Ok : Result::Unsupported;
}

jnext::dbg::RunState DzrpTarget::run_state() const {
    jnext::dbg::RunState st;
    st.paused = lost_ || !cl_->running();
    st.pc     = regs_valid_ ? regs_.PC : pc_hint_;
    return st;
}

// ── the cache ───────────────────────────────────────────────────────────────

void DzrpTarget::drop_cpu_lines() const {
    for (auto it = lines_.begin(); it != lines_.end();) {
        if (it->first < 0x10000) it = lines_.erase(it);
        else ++it;
    }
}

void DzrpTarget::drop_cache() const {
    regs_valid_ = false;
    lines_.clear();
    nr_valid_.fill(false);
    spr_valid_  = false;
    pat_valid_  = false;
    pal_valid_.fill(false);
    clip_valid_ = false;
}

bool DzrpTarget::store_registers(const Bytes& p) const {
    if (p.size() != kRegisterReply || p[28] != 8) {
        lose("protocol error: CMD_GET_REGISTERS reply of " + std::to_string(p.size()) +
             " bytes (" + std::to_string(kRegisterReply) + " with 8 slots expected)");
        return false;
    }
    auto w = [&](std::size_t at) { return static_cast<uint16_t>(p[at] | (p[at + 1] << 8)); };
    Z80Registers r{};
    r.PC = w(0);  r.SP = w(2);  r.AF = w(4);  r.BC = w(6);  r.DE = w(8);  r.HL = w(10);
    r.IX = w(12); r.IY = w(14); r.AF2 = w(16); r.BC2 = w(18); r.DE2 = w(20); r.HL2 = w(22);
    r.R = p[24];  r.I = p[25];  r.IM = p[26];
    regs_ = r;
    for (std::size_t i = 0; i < 8; ++i) slot_bytes_[i] = p[29 + i];
    regs_valid_ = true;
    pc_hint_    = r.PC;
    sp_hint_    = r.SP;
    sp_known_   = true;
    return true;
}

std::vector<std::pair<std::uint8_t, DzrpTarget::Bytes>> DzrpTarget::mem_requests(
    const std::vector<int>& lines) const {
    std::vector<std::pair<std::uint8_t, Bytes>> out;
    const auto runs = runs_of(lines);
    if (runs.empty()) return out;
    if (cl_->in_set(CMD_READ_MEM_BLOCKS)) {
        std::uint32_t total = 0;
        for (const auto& r : runs) total += static_cast<std::uint32_t>(r.second) * kLineBytes;
        Bytes p;
        const std::uint32_t resp = total + 1;
        for (int sh : {0, 8, 16, 24}) p.push_back(static_cast<std::uint8_t>(resp >> sh));
        for (const auto& r : runs) {
            put16(p, static_cast<std::uint16_t>(r.first * kLineBytes));
            put16(p, static_cast<std::uint16_t>(r.second * kLineBytes));
        }
        out.push_back({CMD_READ_MEM_BLOCKS, std::move(p)});
    } else {
        for (const auto& r : runs) {
            Bytes p = {0};
            put16(p, static_cast<std::uint16_t>(r.first * kLineBytes));
            put16(p, static_cast<std::uint16_t>(r.second * kLineBytes));
            out.push_back({CMD_READ_MEM, std::move(p)});
        }
    }
    return out;
}

bool DzrpTarget::store_mem_replies(const std::vector<int>& lines, const std::vector<Reply>& replies,
                                   std::size_t first) const {
    if (lines.empty()) return true;
    Bytes all;
    if (cl_->in_set(CMD_READ_MEM_BLOCKS)) {
        if (!length_is(replies[first], lines.size() * kLineBytes)) return false;
        all = replies[first].payload;
    } else {
        const auto runs = runs_of(lines);
        for (std::size_t i = 0; i < runs.size(); ++i) {
            if (!length_is(replies[first + i], static_cast<std::size_t>(runs[i].second) * kLineBytes))
                return false;
            all.insert(all.end(), replies[first + i].payload.begin(), replies[first + i].payload.end());
        }
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        Line l;
        std::copy(all.begin() + static_cast<long>(i * kLineBytes),
                  all.begin() + static_cast<long>((i + 1) * kLineBytes), l.begin());
        lines_[static_cast<uint32_t>(lines[i])] = l;
    }
    return true;
}

bool DzrpTarget::fetch(bool want_regs, const std::vector<int>& cpu_lines) const {
    std::vector<std::pair<std::uint8_t, Bytes>> reqs;
    if (want_regs) reqs.push_back({CMD_GET_REGISTERS, {}});
    const std::size_t first = reqs.size();
    for (auto& r : mem_requests(cpu_lines)) reqs.push_back(std::move(r));
    if (reqs.empty()) return true;
    const std::vector<Reply> replies = cl_->request_all(std::move(reqs));
    for (const Reply& r : replies)
        if (!good(r)) return false;
    if (want_regs && (!length_is(replies[0], kRegisterReply) || !store_registers(replies[0].payload)))
        return false;
    return store_mem_replies(cpu_lines, replies, first);
}

void DzrpTarget::prime() const {
    if (!burst_pending_ || lost_ || cl_->running()) return;
    burst_pending_ = false;
    std::set<int> lines;
    auto around = [&](uint32_t from, uint32_t to) {
        to = std::min<uint32_t>(to, 0xFFFF);
        for (uint32_t a = from >> 8; a <= (to >> 8); ++a) lines.insert(static_cast<int>(a));
    };
    if (have_hint_) around(pc_hint_ >= 0x10 ? pc_hint_ - 0x10u : 0u, pc_hint_ + 0x40u);
    if (sp_known_) around(sp_hint_, sp_hint_ + 0x20u);
    fetch(true, std::vector<int>(lines.begin(), lines.end()));
}

bool DzrpTarget::ensure_registers() const {
    if (regs_valid_) return true;
    return fetch(true, {});
}

std::vector<int> DzrpTarget::missing_cpu_lines(uint32_t addr, size_t n) const {
    std::vector<int> out;
    if (n == 0) return out;
    const uint32_t start = (addr & 0xFFFF) >> 8;
    const size_t   count = std::min<size_t>(256, ((addr & 0xFF) + n + 255) / 256);
    std::set<int>  want;
    for (size_t i = 0; i < count; ++i) want.insert(static_cast<int>((start + i) & 0xFF));
    for (int l : want)
        if (!lines_.count(static_cast<uint32_t>(l))) out.push_back(l);
    return out;
}

bool DzrpTarget::fetch_page_lines(uint8_t page, const std::vector<int>& lines) const {
    const auto runs = runs_of(lines);
    if (runs.empty()) return true;
    std::vector<std::pair<std::uint8_t, Bytes>> reqs;
    for (const auto& r : runs) {
        Bytes p = {page};
        put16(p, static_cast<std::uint16_t>(r.first * kLineBytes));
        put16(p, static_cast<std::uint16_t>(r.second * kLineBytes));
        reqs.push_back({CMD_READ_BANK_MEM, std::move(p)});
    }
    const std::vector<Reply> replies = cl_->request_all(std::move(reqs));
    for (const Reply& r : replies)
        if (!good(r)) return false;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (!length_is(replies[i], static_cast<std::size_t>(runs[i].second) * kLineBytes)) return false;
        for (int k = 0; k < runs[i].second; ++k) {
            Line l;
            std::copy(replies[i].payload.begin() + k * static_cast<long>(kLineBytes),
                      replies[i].payload.begin() + (k + 1) * static_cast<long>(kLineBytes), l.begin());
            lines_[0x10000u + page * 32u + static_cast<uint32_t>(runs[i].first + k)] = l;
        }
    }
    return true;
}

bool DzrpTarget::fetch_nr_line(unsigned line) const {
    std::vector<std::pair<std::uint8_t, Bytes>> reqs;
    for (unsigned i = 0; i < 16; ++i) reqs.push_back({CMD_GET_TBBLUE_REG, {static_cast<std::uint8_t>(line * 16 + i)}});
    const std::vector<Reply> replies = cl_->request_all(std::move(reqs));
    for (const Reply& r : replies)
        if (!good(r)) return false;
    for (unsigned i = 0; i < 16; ++i) {
        if (!length_is(replies[i], 1)) return false;
        nr_[line * 16 + i] = replies[i].payload[0];
    }
    nr_valid_[line] = true;
    return true;
}

bool DzrpTarget::fetch_sprites() const {
    if (spr_valid_) return true;
    const Reply r = cl_->request(CMD_GET_SPRITES, {0, static_cast<std::uint8_t>(jnext::dbg::SPRITE_COUNT)});
    if (!good(r) || !length_is(r, jnext::dbg::SPRITE_COUNT * jnext::dbg::SPRITE_ATTR_BYTES)) return false;
    spr_       = r.payload;
    spr_valid_ = true;
    return true;
}

bool DzrpTarget::fetch_patterns() const {
    if (pat_valid_) return true;
    const Reply r = cl_->request(CMD_GET_SPRITE_PATTERNS, {0, 64});
    if (!good(r) || !length_is(r, jnext::dbg::PATTERN_RAM_BYTES)) return false;
    pat_       = r.payload;
    pat_valid_ = true;
    return true;
}

bool DzrpTarget::fetch_palette(int bank) const {
    if (pal_valid_[static_cast<std::size_t>(bank)]) return true;
    const Reply r = cl_->request(CMD_GET_SPRITES_PALETTE, {static_cast<std::uint8_t>(bank)});
    if (!good(r) || !length_is(r, 512)) return false;
    for (std::size_t i = 0; i < 256; ++i)
        pal_[static_cast<std::size_t>(bank)][i] =
            static_cast<uint16_t>((r.payload[2 * i] << 1) | (r.payload[2 * i + 1] & 1));
    pal_valid_[static_cast<std::size_t>(bank)] = true;
    return true;
}

bool DzrpTarget::fetch_clip() const {
    if (clip_valid_) return true;
    const Reply r = cl_->request(CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL);
    if (!good(r) || !length_is(r, 5)) return false;
    clip_.x1   = r.payload[0];
    clip_.x2   = r.payload[1];
    clip_.y1   = r.payload[2];
    clip_.y2   = r.payload[3];
    clip_valid_ = true;
    return true;
}

// ── registers ───────────────────────────────────────────────────────────────

Z80Registers DzrpTarget::registers() const {
    if (!usable()) return Z80Registers{};
    prime();
    if (!ensure_registers()) return Z80Registers{};
    return regs_;
}

Result DzrpTarget::set_register(RegId reg, uint16_t value) {
    if (!usable()) return Result::Unsupported;
    const int idx = dzrp_index_of(reg);
    if (idx < 0) return Result::Unsupported;   // IFF1 / IFF2: CMD_INTERRUPT_ON_OFF sets both
    prime();
    Bytes p = {static_cast<std::uint8_t>(idx)};
    put16(p, value);
    const std::vector<Reply> r = cl_->request_all({{CMD_SET_REGISTER, std::move(p)}, {CMD_GET_REGISTERS, {}}});
    if (!good(r[0]) || !good(r[1])) return Result::Unsupported;
    if (!length_is(r[1], kRegisterReply) || !store_registers(r[1].payload)) return Result::Unsupported;
    return Result::Ok;
}

// ── memory ──────────────────────────────────────────────────────────────────

Expected<size_t> DzrpTarget::read(MemSpace space, uint32_t addr, size_t n, uint8_t* buf) const {
    using E = Expected<size_t>;
    if (space.kind == MemSpace::Kind::Page) {
        if (space.index >= 0xE0) return E{Result::InvalidPage, 0};
        if (!usable()) return E{Result::Unsupported, 0};
        const size_t avail = addr < kPageBytes ? std::min<size_t>(n, kPageBytes - addr) : 0;
        if (avail > 0) {
            prime();
            const uint8_t page = static_cast<uint8_t>(space.index);
            std::vector<int> miss;
            for (size_t l = addr >> 8; l <= (addr + avail - 1) >> 8; ++l)
                if (!lines_.count(0x10000u + page * 32u + static_cast<uint32_t>(l)))
                    miss.push_back(static_cast<int>(l));
            if (!fetch_page_lines(page, miss)) return E{Result::Unsupported, 0};
            for (size_t i = 0; i < avail; ++i) {
                const size_t a = addr + i;
                buf[i] = lines_[0x10000u + page * 32u + static_cast<uint32_t>(a >> 8)][a & 0xFF];
            }
        }
        return E{avail == n ? Result::Ok : Result::RefusedUnavailable, avail};
    }
    if (space.kind != MemSpace::Kind::Cpu) return E{Result::Unsupported, 0};   // Rom, DivMmcRam, ...
    if (!usable()) return E{Result::Unsupported, 0};
    if (n == 0) return E{Result::Ok, 0};
    prime();
    const std::vector<int> miss = missing_cpu_lines(addr, n);
    if (!miss.empty() && !fetch(false, miss)) return E{Result::Unsupported, 0};
    for (size_t i = 0; i < n; ++i) {
        const uint32_t a = static_cast<uint32_t>((addr + i) & 0xFFFF);
        buf[i]           = lines_[a >> 8][a & 0xFF];
    }
    return E{Result::Ok, n};
}

Expected<size_t> DzrpTarget::write(MemSpace space, uint32_t addr, size_t n, const uint8_t* buf) {
    using E = Expected<size_t>;
    if (space.kind == MemSpace::Kind::Page) {
        if (space.index >= 0xE0) return E{Result::InvalidPage, 0};
        if (!usable()) return E{Result::Unsupported, 0};
        const size_t m = addr < kPageBytes ? std::min<size_t>(n, kPageBytes - addr) : 0;
        if (m > 0) {
            prime();
            const uint8_t page = static_cast<uint8_t>(space.index);
            Bytes w = {page};
            put16(w, static_cast<uint16_t>(addr));
            w.insert(w.end(), buf, buf + m);
            std::vector<int> lines;
            for (size_t l = addr >> 8; l <= (addr + m - 1) >> 8; ++l) lines.push_back(static_cast<int>(l));
            std::vector<std::pair<std::uint8_t, Bytes>> reqs = {{CMD_WRITE_BANK_MEM, std::move(w)}};
            for (const auto& r : runs_of(lines)) {
                Bytes p = {page};
                put16(p, static_cast<uint16_t>(r.first * kLineBytes));
                put16(p, static_cast<uint16_t>(r.second * kLineBytes));
                reqs.push_back({CMD_READ_BANK_MEM, std::move(p)});
            }
            const std::vector<Reply> replies = cl_->request_all(std::move(reqs));
            for (const Reply& r : replies)
                if (!good(r)) return E{Result::Unsupported, 0};
            const auto runs = runs_of(lines);
            for (std::size_t i = 0; i < runs.size(); ++i) {
                if (!length_is(replies[i + 1], static_cast<std::size_t>(runs[i].second) * kLineBytes))
                    return E{Result::Unsupported, 0};
                for (int k = 0; k < runs[i].second; ++k) {
                    Line l;
                    std::copy(replies[i + 1].payload.begin() + k * static_cast<long>(kLineBytes),
                              replies[i + 1].payload.begin() + (k + 1) * static_cast<long>(kLineBytes), l.begin());
                    lines_[0x10000u + page * 32u + static_cast<uint32_t>(runs[i].first + k)] = l;
                }
            }
            drop_cpu_lines();   // a slot may map this page
        }
        return E{m == n ? Result::Ok : Result::RefusedUnavailable, m};
    }
    if (space.kind != MemSpace::Kind::Cpu) return E{Result::Unsupported, 0};
    if (!usable()) return E{Result::Unsupported, 0};
    if (n == 0) return E{Result::Ok, 0};
    prime();
    if (!ensure_registers()) return E{Result::Unsupported, 0};

    Bytes w = {0};
    put16(w, static_cast<uint16_t>(addr & 0xFFFF));
    w.insert(w.end(), buf, buf + n);
    // The lines the write can change, re-read in the same round trip.
    std::set<int> touched;
    {
        const uint32_t start = (addr & 0xFFFF) >> 8;
        const size_t   count = std::min<size_t>(256, ((addr & 0xFF) + n + 255) / 256);
        for (size_t i = 0; i < count; ++i) touched.insert(static_cast<int>((start + i) & 0xFF));
    }
    const std::vector<int> lines(touched.begin(), touched.end());
    std::vector<std::pair<std::uint8_t, Bytes>> reqs = {{CMD_WRITE_MEM, std::move(w)}};
    for (auto& r : mem_requests(lines)) reqs.push_back(std::move(r));
    const std::vector<Reply> replies = cl_->request_all(std::move(reqs));
    for (const Reply& r : replies)
        if (!good(r)) return E{Result::Unsupported, 0};
    if (!store_mem_replies(lines, replies, 1)) return E{Result::Unsupported, 0};
    // Page lines may alias what was written.
    for (auto it = lines_.begin(); it != lines_.end();) {
        if (it->first >= 0x10000) it = lines_.erase(it);
        else ++it;
    }
    // What LANDED: the bytes that fell in slots the slot view reports as RAM.
    size_t landed = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t a = static_cast<uint32_t>((addr + i) & 0xFFFF);
        if (slot_bytes_[a >> 13] != 0xFF) ++landed;
    }
    return E{landed == n ? Result::Ok : Result::RefusedReadOnly, landed};
}

// ── banking ─────────────────────────────────────────────────────────────────

std::array<SlotInfo, 8> DzrpTarget::mmu_slots() const {
    std::array<SlotInfo, 8> out{};
    if (!usable()) return out;
    prime();
    if (!ensure_registers()) return out;
    for (std::size_t i = 0; i < 8; ++i) {
        const std::uint8_t v = slot_bytes_[i];
        SlotInfo&          s = out[i];
        s.nr_page            = v;
        s.effective_page     = v;
        if (v == 0xFF) {
            // The wire says "ROM" without the ROM's index: the bytes are named through
            // the CPU view, where the slot is. A guessed Rom{k} would mis-key ROM source maps.
            s.is_rom       = true;
            s.space        = MemSpace::cpu();
            s.space_offset = static_cast<uint16_t>(i << 13);
            s.now          = MemSpace::cpu();
        } else {
            s.is_rom       = false;
            s.space        = MemSpace::page(v);
            s.space_offset = 0;
            s.now          = MemSpace::page(v);
        }
    }
    return out;
}

Result DzrpTarget::set_mmu_slot(int slot, uint8_t page) {
    if (!usable() || slot < 0 || slot > 7) return Result::Unsupported;
    prime();
    const std::vector<Reply> r = cl_->request_all(
        {{CMD_SET_SLOT, {static_cast<std::uint8_t>(slot), page}}, {CMD_GET_REGISTERS, {}}});
    if (!good(r[0]) || !good(r[1])) return Result::Unsupported;
    if (!length_is(r[0], 1) || !length_is(r[1], kRegisterReply)) return Result::Unsupported;
    drop_cache();   // the mapping moved: memory, NextREGs and sprites are all stale
    if (!store_registers(r[1].payload)) return Result::Unsupported;
    return r[0].payload[0] == 0 ? Result::Ok : Result::RefusedUnavailable;
}

// ── NextREG and ports ───────────────────────────────────────────────────────

uint8_t DzrpTarget::nextreg_read(uint8_t reg) const {
    if (!usable()) return 0;
    prime();
    const unsigned line = reg >> 4;
    if (!nr_valid_[line] && !fetch_nr_line(line)) return 0;
    return nr_[reg];
}

Result DzrpTarget::nextreg_write(uint8_t reg, uint8_t value) {
    if (!usable() || !cl_->in_set(CMD_SET_NEXTREGS)) return Result::Unsupported;
    prime();
    const Result r = submit_ack(CMD_SET_NEXTREGS, {reg, value});
    drop_cache();   // a NextREG write can remap memory, move sprites, page the ROM
    return r;
}

Expected<uint8_t> DzrpTarget::port_in(uint16_t port) {
    if (!usable()) return make_refused<uint8_t>(Result::Unsupported);
    prime();
    Bytes p;
    put16(p, port);
    const Reply r = cl_->request(CMD_READ_PORT, std::move(p));
    if (!good(r)) return make_refused<uint8_t>(Result::Unsupported);
    drop_cache();   // an IN can page memory
    if (r.payload.empty()) return make_refused<uint8_t>(Result::RefusedUnavailable);
    if (!length_is(r, 1)) return make_refused<uint8_t>(Result::Unsupported);
    return make_ok<uint8_t>(r.payload[0]);
}

Result DzrpTarget::port_out(uint16_t port, uint8_t value) {
    if (!usable()) return Result::Unsupported;
    prime();
    Bytes p;
    put16(p, port);
    p.push_back(value);
    const Result r = submit_ack(CMD_WRITE_PORT, std::move(p));
    drop_cache();
    return r;
}

// ── sprites ─────────────────────────────────────────────────────────────────

std::vector<jnext::dbg::SpriteInfo> DzrpTarget::sprites() const {
    std::vector<jnext::dbg::SpriteInfo> out;
    if (!usable()) return out;
    prime();
    if (!fetch_sprites()) return out;
    out.reserve(jnext::dbg::SPRITE_COUNT);
    for (std::size_t i = 0; i < jnext::dbg::SPRITE_COUNT; ++i)
        out.push_back(SpriteEngine::sprite_info_from_attr(spr_.data() + i * jnext::dbg::SPRITE_ATTR_BYTES));
    return out;
}

Expected<std::array<uint8_t, jnext::dbg::SPRITE_ATTR_BYTES>> DzrpTarget::sprite_attr_raw(uint8_t idx) const {
    using Arr = std::array<uint8_t, jnext::dbg::SPRITE_ATTR_BYTES>;
    if (!usable()) return make_refused<Arr>(Result::Unsupported);
    Arr a{};
    if (idx >= jnext::dbg::SPRITE_COUNT) return make_ok<Arr>(a);   // zeros, as the engine answers
    prime();
    if (!fetch_sprites()) return make_refused<Arr>(Result::Unsupported);
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = spr_[idx * jnext::dbg::SPRITE_ATTR_BYTES + i];
    return make_ok<Arr>(a);
}

ConstBytes DzrpTarget::pattern_ram() const {
    if (!usable()) return ConstBytes{};
    prime();
    if (!fetch_patterns()) return ConstBytes{};
    return ConstBytes{pat_.data(), pat_.size()};
}

Expected<uint16_t> DzrpTarget::sprite_palette_rgb333(int bank, uint8_t index) const {
    if (!usable()) return make_refused<uint16_t>(Result::Unsupported);
    if (bank < 0 || bank > 1) return make_refused<uint16_t>(Result::RefusedUnavailable);
    prime();
    if (!fetch_palette(bank)) return make_refused<uint16_t>(Result::Unsupported);
    return make_ok<uint16_t>(pal_[static_cast<std::size_t>(bank)][index]);
}

jnext::dbg::ClipWindow DzrpTarget::sprite_clip() const {
    if (!usable()) return jnext::dbg::ClipWindow{};
    prime();
    if (!fetch_clip()) return jnext::dbg::ClipWindow{};
    return clip_;
}

// ── run control ─────────────────────────────────────────────────────────────

Result DzrpTarget::resume() {
    if (!usable() || !caps_.has(Capability::Run)) return Result::Unsupported;
    // Eleven bytes, as DeZog sends them and as dezogif_ng reads them: breakpoint 1 and 2
    // (enable, address) all off, alternate command 0 and its range.
    const Result r = submit_ack(CMD_CONTINUE, Bytes(11, 0));
    if (r != Result::Ok) return r;
    drop_cache();
    stop_.reset();
    owed_ = 1;
    return Result::Ok;
}

Result DzrpTarget::pause() {
    if (!usable() || !caps_.has(Capability::Pause)) return Result::Unsupported;
    const bool was_running = cl_->running();
    const Result r = submit_ack(CMD_PAUSE, {});
    if (r != Result::Ok) return r;
    // A target we believe paused may have been resumed by the remote's own GUI: its stop
    // is then owed to this pause. Harmless when it was not (nothing comes; the next
    // resume sets the count again).
    if (!was_running) owed_ = 1;
    return Result::Ok;
}

bool DzrpTarget::take_stop(TargetStop& out) {
    if (!stop_) return false;
    out = std::move(*stop_);
    stop_.reset();
    return true;
}

void DzrpTarget::on_pause_notification(const PauseNotification& n) {
    if (owed_ <= 0) {
        Log::debugger()->warn("dzrp target: NTF_PAUSE (reason {}) with none owed - dropped", n.reason);
        return;
    }
    --owed_;
    TargetStop s;
    s.addr = n.addr;
    s.page = (n.bank1 >= 1 && n.bank1 <= 224) ? static_cast<uint16_t>(n.bank1 - 1) : PAGE_ANY;
    switch (n.reason) {
        case BREAK_MANUAL:   s.why = TargetStop::Why::Manual; break;
        case BREAK_BP:       s.why = TargetStop::Why::Breakpoint; break;
        case BREAK_WP_READ:  s.why = TargetStop::Why::WatchRead; break;
        case BREAK_WP_WRITE: s.why = TargetStop::Why::WatchWrite; break;
        case BREAK_OTHER:    s.why = TargetStop::Why::Other; s.text = n.text; break;
        default:
            // No temporary breakpoint is ever sent from here, so a reason 0 is as unknown
            // as any other value.
            s.why  = TargetStop::Why::Other;
            s.text = "stop reason " + std::to_string(n.reason) + (n.text.empty() ? "" : ": " + n.text);
            break;
    }
    drop_cache();
    burst_pending_ = true;
    have_hint_     = s.why == TargetStop::Why::Breakpoint;
    if (have_hint_) pc_hint_ = s.addr;
    stop_ = std::move(s);
}

ServiceStep DzrpTarget::service_once(int wait_ms) {
    if (lost_) return ServiceStep::Idle;
    cl_->poll(wait_ms);
    const SessionState st = cl_->state();
    if (st == SessionState::Failed || st == SessionState::Closed) {
        lose(cl_->error().empty() ? std::string("the session ended") : cl_->error());
        return ServiceStep::Idle;
    }
    if (cl_->running()) drop_cache();   // read while running: fetched at most once per pump
    bool took = false;
    while (auto n = cl_->take_pause()) {
        took = true;
        on_pause_notification(*n);
    }
    return took ? ServiceStep::Serviced : ServiceStep::Idle;
}

// ── breakpoints and watchpoints ─────────────────────────────────────────────

Expected<uint32_t> DzrpTarget::add_breakpoint(uint16_t addr, uint16_t page) {
    if (!usable() || !caps_.has(Capability::ExecuteBreakpoints))
        return make_refused<uint32_t>(Result::Unsupported);
    if (page != PAGE_ANY && page > 223) return make_refused<uint32_t>(Result::RefusedUnavailable);
    const std::uint8_t bank1 = page == PAGE_ANY ? 0 : static_cast<std::uint8_t>(page + 1);
    Bytes p;
    put16(p, addr);
    p.push_back(bank1);
    p.push_back(0);   // the condition, empty: conditions run in this client
    const Reply r = cl_->request(CMD_ADD_BREAKPOINT, std::move(p));
    if (!good(r) || !length_is(r, 2)) return make_refused<uint32_t>(Result::Unsupported);
    const uint16_t id = static_cast<uint16_t>(r.payload[0] | (r.payload[1] << 8));
    if (id == 0) return make_refused<uint32_t>(Result::RefusedUnavailable);
    if (bps_.count(id)) {
        lose("protocol error: breakpoint id " + std::to_string(id) + " issued twice");
        return make_refused<uint32_t>(Result::Unsupported);
    }
    bps_[id] = {addr, bank1};
    return make_ok<uint32_t>(id);
}

Result DzrpTarget::remove_breakpoint(uint32_t handle) {
    if (!usable() || !caps_.has(Capability::ExecuteBreakpoints)) return Result::Unsupported;
    const auto it = bps_.find(static_cast<uint16_t>(handle));
    if (handle > 0xFFFF || it == bps_.end()) return Result::RefusedUnavailable;
    Bytes p;
    put16(p, it->first);
    bps_.erase(it);
    return submit_ack(CMD_REMOVE_BREAKPOINT, std::move(p));
}

Expected<uint32_t> DzrpTarget::add_watchpoint(uint16_t lo, uint16_t hi, uint16_t page, Access access) {
    if (!usable() || !caps_.has(Capability::Watchpoints)) return make_refused<uint32_t>(Result::Unsupported);
    const std::uint8_t bits = static_cast<std::uint8_t>(access) & 3;
    if (hi < lo || bits == 0 || (page != PAGE_ANY && page > 223))
        return make_refused<uint32_t>(Result::RefusedUnavailable);
    Watch w;
    w.addr   = lo;
    w.bank1  = page == PAGE_ANY ? 0 : static_cast<std::uint8_t>(page + 1);
    w.size   = static_cast<uint16_t>(hi - lo + 1);
    w.access = bits;
    Bytes p;
    put16(p, w.addr);
    p.push_back(w.bank1);
    put16(p, w.size);
    p.push_back(w.access);
    const Reply r = cl_->request(CMD_ADD_WATCHPOINT, std::move(p));
    if (!good(r) || !length_is(r, 1)) return make_refused<uint32_t>(Result::Unsupported);
    if (r.payload[0] != 0) return make_refused<uint32_t>(Result::RefusedUnavailable);
    const uint32_t h = next_wp_++;
    wps_[h]          = w;
    return make_ok<uint32_t>(h);
}

Result DzrpTarget::remove_watchpoint(uint32_t handle) {
    if (!usable() || !caps_.has(Capability::Watchpoints)) return Result::Unsupported;
    const auto it = wps_.find(handle);
    if (it == wps_.end()) return Result::RefusedUnavailable;
    const Watch w = it->second;
    wps_.erase(it);
    Bytes p;
    put16(p, w.addr);
    p.push_back(w.bank1);
    put16(p, w.size);
    p.push_back(w.access);
    return submit_ack(CMD_REMOVE_WATCHPOINT, std::move(p));
}

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
