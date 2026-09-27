// ---------------------------------------------------------------------------
// B0's compile gate (GH #276, design §10.1: "compiled and reviewed with no
// bodies behind them").
//
// Headers on their own are compiled by nothing. `src/debug/CMakeLists.txt`
// globs `*.cpp` with `CONFIGURE_DEPENDS`, so THIS translation unit is picked up
// automatically and builds in every one of the four configurations
// `make build-matrix` covers — which is what makes the four published headers
// actually compile rather than merely parse in someone's editor.
//
// IT IS NOT A BEHAVIOUR TEST. B5's `debugger_backend_test` is that, and it is a
// later package. Everything here is a `static_assert` on a property the design
// RELIES ON, and each one names what breaks if it fails:
//
//   * the mirrored enums cannot drift from what they mirror,
//   * the value types a protocol or a latch copies stay trivially copyable,
//   * the sizes and widths the design quotes agree with the code they came from.
//
// Unlike the headers, this file MAY include the internal and the heavy headers
// — it lives inside `jnext_debug`, where `debug_state.h`, `video/palette.h` and
// `video/renderer.h` are all in scope. That asymmetry is the point: the mirror
// is checked HERE so the published headers never have to include the thing they
// mirror.
// ---------------------------------------------------------------------------

#include "debug/debugger.h"
#include "debug/events.h"
#include "debug/inspect.h"
#include "debug/result.h"

#include <type_traits>

// The things the published types mirror. None of these may ever be included by
// the four headers above.
#include "debug/debug_state.h"   // ::StepMode
#include "video/palette.h"       // ::PaletteId
#include "video/renderer.h"      // Renderer::LAYER_*
#include "video/sprites.h"       // SpriteEngine::NUM_SPRITES / PATTERN_RAM_SZ
#include "input/keyboard.h"      // Keyboard::MAX_AUTO_TYPE_KEYS
#include "core/emulator_config.h" // MachineTiming — the two clock domains

namespace jnext {
namespace dbg {
namespace {

// ---------------------------------------------------------------------------
// result.h — the refusal vocabulary
// ---------------------------------------------------------------------------

// `Ok` is zero, so `Result{}` and a zeroed struct mean success. Several
// adapters will map a `Result` onto a protocol error code by index.
static_assert(static_cast<int>(Result::Ok) == 0, "Result::Ok must be 0");
static_assert(ok(Result::Ok), "ok() must accept Ok");
static_assert(!ok(Result::Unsupported), "ok() must reject every refusal");

// EVERY value pinned, not just the two ends. `result.h` says the set is closed
// "in that order" and an adapter may map a `Result` onto a wire error code by
// index, so nine free values in the middle is nine ways for a green build to
// change what a remote reports. Pinning `Ok == 0` and `Unsupported == 10` alone
// let `RefusedRunning` and `RefusedPaused` swap silently.
static_assert(static_cast<int>(Result::Ok)                 ==  0, "");
static_assert(static_cast<int>(Result::RefusedRunning)     ==  1, "Result::RefusedRunning moved off 1 — the set is ordered and adapters map it by index");
static_assert(static_cast<int>(Result::RefusedPaused)      ==  2, "Result::RefusedPaused moved off 2 — the set is ordered and adapters map it by index");
static_assert(static_cast<int>(Result::RefusedCorrupt)     ==  3, "Result::RefusedCorrupt moved off 3 — the set is ordered and adapters map it by index");
static_assert(static_cast<int>(Result::RefusedRzx)         ==  4, "Result::RefusedRzx moved off 4 — the set is ordered and adapters map it by index");
static_assert(static_cast<int>(Result::RefusedUnavailable) ==  5, "Result::RefusedUnavailable moved off 5 — the set is ordered and adapters map it by index");
static_assert(static_cast<int>(Result::RefusedReadOnly)    ==  6, "Result::RefusedReadOnly moved off 6 — the set is ordered and adapters map it by index");
static_assert(static_cast<int>(Result::InvalidPage)        ==  7, "Result::InvalidPage moved off 7 — the set is ordered and adapters map it by index");
static_assert(static_cast<int>(Result::NotAtFrameBoundary) ==  8, "Result::NotAtFrameBoundary moved off 8 — the set is ordered and adapters map it by index");
static_assert(static_cast<int>(Result::NoFrame)            ==  9, "Result::NoFrame moved off 9 — the set is ordered and adapters map it by index");
// The §4 preamble's set is exactly eleven values. A twelfth is a change to the
// design document, and this row is where it gets noticed.
static_assert(static_cast<int>(Result::Unsupported) == 10,
              "Result has grown or shrunk: §4's set is 11 values, Ok..Unsupported");

// `Expected<T>` must not cost more than the pair it is, for the scalar cases
// that go through it per byte-range peek.
static_assert(std::is_trivially_copyable<Expected<size_t>>::value,
              "Expected<scalar> is copied per peek/poke; keep it trivial");
static_assert(std::is_trivially_copyable<Expected<uint8_t>>::value, "");

// ---------------------------------------------------------------------------
// events.h — Event is copied, a lot
// ---------------------------------------------------------------------------

// Every delivery copies an `Event`; `matched[]` and `events_fired_since()` copy
// it again, out of the delivery and into a vector that outlives it. It is a flat
// aggregate precisely so that is free and needs no lifetime rule (which is why
// `host_name` is a buffer and not a pointer).
static_assert(std::is_trivially_copyable<Event>::value,
              "Event must stay trivially copyable: matched[] and "
              "events_fired_since() copy it out of the delivery");
static_assert(std::is_standard_layout<Event>::value, "");

// A bound, not a measurement: if `Event` ever needs to be big, that is a design
// question (a variant, or a per-kind payload table), not a silent regression.
static_assert(sizeof(Event) <= 128, "Event grew past its copy budget");

// `Hit` goes into `PausedInfo::matched` for every subscription that matched.
static_assert(std::is_trivially_copyable<Hit>::value, "");

// 14 kinds, and this now catches an APPEND. `EVENT_KIND_COUNT` is derived from
// the trailing `EventKind::Count` sentinel; deriving it from the last real
// enumerator (`Dma + 1`) was blind to exactly the change it claimed to catch —
// a fifteenth kind added after `Dma` left the count at 14 and every assert here
// kept passing. `EventKindMask` is `uint32_t`, so the mask must also have room
// for every kind.
static_assert(EVENT_KIND_COUNT == 14, "§4.3's table is 14 event kinds");
static_assert(EVENT_KIND_COUNT <= sizeof(EventKindMask) * 8,
              "EventKindMask cannot hold one bit per EventKind");
static_assert(kind_bit(EventKind::Execute) == 0x1u, "");
static_assert(kind_bit(EventKind::Dma) == (1u << 13), "");
// The sentinel is last, so it is not itself a usable kind and no mask bit is
// reserved for it.
static_assert(static_cast<size_t>(EventKind::Dma) + 1 == EVENT_KIND_COUNT,
              "EventKind::Count must sit immediately after the last real kind");

// `Access` doubles as a payload value and a subscription mask, so the bit
// arithmetic has to be right in both directions.
static_assert((Access::Read | Access::Write) == Access::ReadWrite, "");
static_assert(has_read(Access::ReadWrite) && has_write(Access::ReadWrite), "");
static_assert(has_read(Access::Read) && !has_write(Access::Read), "");
static_assert(!has_read(Access::None) && !has_write(Access::None), "");

// `PAGE_ANY` must not collide with a real NR 0x50-0x57 page number (0..223).
static_assert(PAGE_ANY > 223, "PAGE_ANY collides with a real page number");

// ---------------------------------------------------------------------------
// inspect.h — the three mirrors
// ---------------------------------------------------------------------------

// MIRROR 1: `dbg::StepMode` against the internal `::StepMode`
// (`debug/debug_state.h`), which §3.1 keeps unpublished. Value by value, so a
// reordering of either is a build failure rather than a frontend that reports
// "Step Over" while the machine is stepping out.
static_assert(static_cast<int>(StepMode::None) == static_cast<int>(::StepMode::NONE), "");
static_assert(static_cast<int>(StepMode::Into) == static_cast<int>(::StepMode::INTO), "");
static_assert(static_cast<int>(StepMode::Over) == static_cast<int>(::StepMode::OVER), "");
static_assert(static_cast<int>(StepMode::Out) == static_cast<int>(::StepMode::OUT), "");
static_assert(static_cast<int>(StepMode::RunToCycle) ==
              static_cast<int>(::StepMode::RUN_TO_CYCLE), "");
static_assert(static_cast<int>(StepMode::StepBack) ==
              static_cast<int>(::StepMode::STEP_BACK), "");
static_assert(static_cast<int>(StepMode::RunBackToCycle) ==
              static_cast<int>(::StepMode::RUN_BACK_TO_CYCLE), "");

// MIRROR 2: `dbg::PaletteId` 0..7 against `::PaletteId` (`video/palette.h`).
// INS-15 needs a ninth value (`UlaActive`) the hardware enum cannot express, so
// this is a SUPERSET — and the eight it shares must share their numbering, or a
// `set_palette()` writes the wrong bank.
static_assert(static_cast<int>(PaletteId::UlaFirst) ==
              static_cast<int>(::PaletteId::ULA_FIRST), "");
static_assert(static_cast<int>(PaletteId::Layer2First) ==
              static_cast<int>(::PaletteId::LAYER2_FIRST), "");
static_assert(static_cast<int>(PaletteId::SpriteFirst) ==
              static_cast<int>(::PaletteId::SPRITE_FIRST), "");
static_assert(static_cast<int>(PaletteId::TilemapFirst) ==
              static_cast<int>(::PaletteId::TILEMAP_FIRST), "");
static_assert(static_cast<int>(PaletteId::UlaSecond) ==
              static_cast<int>(::PaletteId::ULA_SECOND), "");
static_assert(static_cast<int>(PaletteId::Layer2Second) ==
              static_cast<int>(::PaletteId::LAYER2_SECOND), "");
static_assert(static_cast<int>(PaletteId::SpriteSecond) ==
              static_cast<int>(::PaletteId::SPRITE_SECOND), "");
static_assert(static_cast<int>(PaletteId::TilemapSecond) ==
              static_cast<int>(::PaletteId::TILEMAP_SECOND), "");
static_assert(static_cast<int>(PaletteId::UlaActive) == 8,
              "UlaActive must sit past the eight hardware banks");

// MIRROR 3: the screenshot layer mask against `Renderer::LAYER_*`. CAP-01's
// `layer_mask` is passed straight through to `Renderer::set_layer_mask()`, so
// one wrong bit silently captures the wrong layer.
static_assert(LAYER_MASK_ULA == Renderer::LAYER_ULA, "");
static_assert(LAYER_MASK_LAYER2 == Renderer::LAYER_LAYER2, "");
static_assert(LAYER_MASK_SPRITES == Renderer::LAYER_SPRITES, "");
static_assert(LAYER_MASK_TILES == Renderer::LAYER_TILES, "");
static_assert(LAYER_MASK_ALL == Renderer::LAYER_ALL, "");
static_assert((LAYER_MASK_ULA | LAYER_MASK_LAYER2 | LAYER_MASK_SPRITES |
               LAYER_MASK_TILES) == LAYER_MASK_ALL,
              "the four layer bits must cover LAYER_MASK_ALL");

// `Layer` (INS-14) has no non-Qt counterpart to mirror — its eight views exist
// today only inside `VideoLayerView`, a `Q_OBJECT`. The count is the whole
// signal, so it is derived from `Layer::Count` and catches an append.
static_assert(LAYER_COUNT == 8, "§4 INS-14 / the video panel have eight views");
static_assert(static_cast<size_t>(Layer::Background) + 1 == LAYER_COUNT,
              "Layer::Count must sit immediately after the last real view");

// The four clip windows of NR 0x18-0x1C are a DIFFERENT set from the eight
// render views, which is why `ClipLayer` exists at all. Also from a trailing
// sentinel: `static_cast<int>(ClipLayer::Tilemap) == 3` passed for an appended
// fifth window.
static_assert(CLIP_LAYER_COUNT == 4, "four clip windows (NR 0x18-0x1C)");
static_assert(static_cast<size_t>(ClipLayer::Tilemap) + 1 == CLIP_LAYER_COUNT,
              "ClipLayer::Count must sit immediately after Tilemap");

// ---------------------------------------------------------------------------
// inspect.h — widths and counts taken from the code
// ---------------------------------------------------------------------------

static_assert(SPRITE_COUNT == static_cast<size_t>(SpriteEngine::NUM_SPRITES), "");
static_assert(PATTERN_RAM_BYTES == static_cast<size_t>(SpriteEngine::PATTERN_RAM_SZ),
              "INS-08's '16 KB' must be the engine's actual pattern RAM");
// The oracle that matters: the framebuffer these pixels are written into.
static_assert(RENDER_WIDTH == static_cast<size_t>(Renderer::FB_WIDTH),
              "INS-14's 'width 640' must be the renderer's framebuffer width");
// Secondary, and only a cross-check: the sprite engine's own full pixel width
// is the same number for an unrelated reason.
static_assert(RENDER_WIDTH == static_cast<size_t>(SpriteEngine::DISPLAY_WIDTH), "");
static_assert(SPRITE_ATTR_BYTES == 5, "a sprite is 5 attribute bytes");

// IN-01's overflow contract is stated against this cap, so the two must be the
// same number: `press_key` returns `RefusedUnavailable` plus the count queued
// when an append would cross it.
static_assert(Keyboard::MAX_AUTO_TYPE_KEYS == 16,
              "IN-01's APPEND overflow contract is written against 16");

// INS-10's mute mask is AudioMute's, not a second copy of the bit assignment.
static_assert(AudioMute::AY_ALL == (AudioMute::AY0 | AudioMute::AY1 | AudioMute::AY2), "");
static_assert(AudioMute::ALL == (AudioMute::AY_ALL | AudioMute::DAC | AudioMute::BEEPER), "");
static_assert(AudioMute::NONE == 0, "");

// INS-01 enumerates 12 pairs, 20 eight-bit halves and 5 singletons. From
// `RegId::Count`, so an appended register fires it.
static_assert(REG_ID_COUNT == 37, "INS-01: 12 pairs + 20 halves + I/R/IFF1/IFF2/IM");
static_assert(static_cast<size_t>(RegId::IM) + 1 == REG_ID_COUNT,
              "RegId::Count must sit immediately after IM");

// The two page numbers the VHDL uses as a ROM sentinel, which `MemSpace::page()`
// refuses.
static_assert(PAGE_SENTINEL_ROM_LO == 0xFE && PAGE_SENTINEL_ROM_HI == 0xFF, "");

// INS-20's bit set is one bit per 16-bit PC — 8 KB, which is why `coverage()`
// hands out a const reference instead of a value.
static_assert(CoverageBits{}.size() == 65536, "");

// ---------------------------------------------------------------------------
// inspect.h — MemSpace is a complete address, and comparable
// ---------------------------------------------------------------------------

static_assert(std::is_trivially_copyable<MemSpace>::value, "");
static_assert(MemSpace::cpu().kind == MemSpace::Kind::Cpu, "");
static_assert(MemSpace::page(0x0E).kind == MemSpace::Kind::Page, "");
static_assert(MemSpace::page(0x0E).index == 0x0E, "");
static_assert(MemSpace::rom(3).kind == MemSpace::Kind::Rom, "");
static_assert(MemSpace::rom(3).index == 3, "");
static_assert(MemSpace::page(5) == MemSpace::page(5), "");
static_assert(MemSpace::page(5) != MemSpace::page(6), "");
// A ROM image and a physical page with the same number are NOT the same store —
// this is exactly the confusion REQ-qt-31 caught in review.
static_assert(MemSpace::rom(1) != MemSpace::page(1), "");
// `Cpu` ignores the index, so two default-constructed CPU spaces are equal.
static_assert(MemSpace::cpu() == MemSpace{}, "");
// One width for every page number that crosses between these types, so no hop
// narrows: MemSpace::index, Event::phys_page and EventFilter::page agree.
static_assert(std::is_same<decltype(MemSpace::index), decltype(Event::phys_page)>::value,
              "a page number must not narrow between MemSpace and an Event");
static_assert(std::is_same<decltype(MemSpace::index), decltype(EventFilter::page)>::value,
              "a page number must not narrow between MemSpace and a filter");
static_assert(std::is_same<decltype(MemSpace::index),
                           EventFilter::PageSet::value_type>::value,
              "a page number must not narrow between MemSpace and a page set");
// PAGE_ANY must stay outside the representable page range it guards.
static_assert(PAGE_ANY != PAGE_SENTINEL_ROM_LO && PAGE_ANY != PAGE_SENTINEL_ROM_HI, "");

// ---------------------------------------------------------------------------
// The value types that cross a protocol or sit in a snapshot
// ---------------------------------------------------------------------------

static_assert(std::is_trivially_copyable<SlotInfo>::value,
              "mmu_slots() returns eight of these by value every refresh");
static_assert(std::is_trivially_copyable<Time>::value, "");
static_assert(std::is_trivially_copyable<MachineInfo>::value, "");
static_assert(std::is_trivially_copyable<PagingPorts>::value, "");
static_assert(std::is_trivially_copyable<ClipWindow>::value, "");
static_assert(std::is_trivially_copyable<UlaScreenRegs>::value, "");
static_assert(std::is_trivially_copyable<RewindRange>::value, "");
static_assert(std::is_trivially_copyable<MatrixKey>::value, "");
static_assert(std::is_trivially_copyable<InputState>::value,
              "INS-16 is sampled once per frame by the #20 recorder");
static_assert(std::is_trivially_copyable<SpriteInfo>::value, "");
static_assert(std::is_trivially_copyable<ServiceHint>::value, "");
static_assert(std::is_trivially_copyable<PumpBudget>::value, "");

// `Z80Registers` is reused as-is (INS-01) and the register file is copied by
// value on every refresh and every trace entry.
static_assert(std::is_trivially_copyable<Z80Registers>::value, "");

// INS-13's `TraceEntry` is reused as-is and lives in a ring buffer the design
// sizes in bytes per entry.
static_assert(std::is_trivially_copyable<TraceEntry>::value, "");

// `RunState` and `PauseReason` deliberately are NOT trivially copyable:
// `PauseReason::Script` carries the script's message as a `std::string`, which
// is what lets a regression row tell a script verdict from a harness fault.
static_assert(!std::is_trivially_copyable<PauseReason>::value,
              "EXPECTED NOT TO BE TRIVIAL (this is not a failure of triviality): "
              "PauseReason carries the Script reason's text as a std::string, "
              "which is what lets a regression row tell a script verdict from a "
              "harness fault. If this fires, the string was removed.");

// ---------------------------------------------------------------------------
// debugger.h — the interfaces have no bodies to inherit by accident
// ---------------------------------------------------------------------------

static_assert(std::is_abstract<Listener>::value,
              "Listener must be pure virtual: a silently ignored notification "
              "is the failure mode a default override invites");
static_assert(std::is_abstract<Service>::value, "");

// PER-METHOD, not per-class. `is_abstract<Listener>` above is necessary and far
// from sufficient: give any ONE of the seven methods a default empty body and
// the class is still abstract because the other six are still pure, so that
// assert keeps passing while the header's claim — "adding a notification later
// is meant to break every implementer" — quietly stops holding for that one.
//
// So: seven stubs, each overriding SIX of the seven and omitting a different
// one, each asserted STILL ABSTRACT. Together they say every method is pure.
// Nothing is ever instantiated; these are type queries only.
#define DBG_ON_PAUSED void on_paused(const PausedInfo&) override {}
#define DBG_ON_RESUMED void on_resumed(ClientId) override {}
#define DBG_ON_RESET void on_reset(ResetKind) override {}
#define DBG_ON_FRAME void on_frame_ended(uint32_t) override {}
#define DBG_ON_SUBS void on_subscriptions_changed(EventKindMask) override {}
#define DBG_ON_EXIT void on_exit_requested(int) override {}
#define DBG_ON_LOG void on_log(LogLevel, const std::string&) override {}

struct OmitPaused  : Listener {                DBG_ON_RESUMED DBG_ON_RESET DBG_ON_FRAME DBG_ON_SUBS DBG_ON_EXIT DBG_ON_LOG };
struct OmitResumed : Listener { DBG_ON_PAUSED                 DBG_ON_RESET DBG_ON_FRAME DBG_ON_SUBS DBG_ON_EXIT DBG_ON_LOG };
struct OmitReset   : Listener { DBG_ON_PAUSED DBG_ON_RESUMED               DBG_ON_FRAME DBG_ON_SUBS DBG_ON_EXIT DBG_ON_LOG };
struct OmitFrame   : Listener { DBG_ON_PAUSED DBG_ON_RESUMED DBG_ON_RESET               DBG_ON_SUBS DBG_ON_EXIT DBG_ON_LOG };
struct OmitSubs    : Listener { DBG_ON_PAUSED DBG_ON_RESUMED DBG_ON_RESET DBG_ON_FRAME              DBG_ON_EXIT DBG_ON_LOG };
struct OmitExit    : Listener { DBG_ON_PAUSED DBG_ON_RESUMED DBG_ON_RESET DBG_ON_FRAME DBG_ON_SUBS              DBG_ON_LOG };
struct OmitLog     : Listener { DBG_ON_PAUSED DBG_ON_RESUMED DBG_ON_RESET DBG_ON_FRAME DBG_ON_SUBS DBG_ON_EXIT             };

static_assert(std::is_abstract<OmitPaused>::value,  "Listener::on_paused is not pure");
static_assert(std::is_abstract<OmitResumed>::value, "Listener::on_resumed is not pure");
static_assert(std::is_abstract<OmitReset>::value,   "Listener::on_reset is not pure");
static_assert(std::is_abstract<OmitFrame>::value,   "Listener::on_frame_ended is not pure");
static_assert(std::is_abstract<OmitSubs>::value,    "Listener::on_subscriptions_changed is not pure");
static_assert(std::is_abstract<OmitExit>::value,    "Listener::on_exit_requested is not pure");
static_assert(std::is_abstract<OmitLog>::value,     "Listener::on_log is not pure");

// The other direction, so the seven above cannot pass for a trivial reason (a
// typo'd signature would make a stub override nothing and stay abstract for the
// WRONG reason): all seven together are concrete.
struct AllSeven : Listener {
    DBG_ON_PAUSED DBG_ON_RESUMED DBG_ON_RESET DBG_ON_FRAME DBG_ON_SUBS DBG_ON_EXIT DBG_ON_LOG
};
static_assert(!std::is_abstract<AllSeven>::value,
              "a stub overriding all seven must be concrete — if this fires, one "
              "of the seven signatures above does not match the interface and the "
              "OmitX asserts are passing for the wrong reason");
static_assert(std::is_base_of<Listener, AllSeven>::value, "");

#undef DBG_ON_PAUSED
#undef DBG_ON_RESUMED
#undef DBG_ON_RESET
#undef DBG_ON_FRAME
#undef DBG_ON_SUBS
#undef DBG_ON_EXIT
#undef DBG_ON_LOG

// ---------------------------------------------------------------------------
// inspect.h — MachineInfo's two clock domains (B4)
// ---------------------------------------------------------------------------

// The backend fills MachineInfo from MachineTiming (`core/emulator_config.h`),
// so the field NAMES and TYPES are pinned against it: a rename on either side
// is a build failure, and the names are the whole defence against confusing
// 3.5 MHz T-states with 28 MHz master cycles (4x apart, both once called
// "cycles" in this struct).
static_assert(std::is_same<decltype(MachineInfo::tstates_per_line),
                           decltype(MachineTiming::tstates_per_line)>::value, "");
static_assert(std::is_same<decltype(MachineInfo::tstates_per_frame),
                           decltype(MachineTiming::tstates_per_frame)>::value, "");
static_assert(std::is_same<decltype(MachineInfo::master_cycles_per_line),
                           decltype(MachineTiming::master_cycles_per_line)>::value, "");
static_assert(std::is_same<decltype(MachineInfo::master_cycles_per_frame),
                           decltype(MachineTiming::master_cycles_per_frame)>::value, "");
// `lines` is §4 INS-19's spelling of `MachineTiming::lines_per_frame`; the names
// differ deliberately, so only the type is pinned.
static_assert(std::is_same<decltype(MachineInfo::lines),
                           decltype(MachineTiming::lines_per_frame)>::value, "");

// And the RELATION between the two domains, which is what a reader gets wrong:
// one T-state at 3.5 MHz is eight 28 MHz master cycles. Checked on a real
// machine's constants rather than asserted in prose.
namespace {
constexpr MachineTiming t48 = machine_timing(MachineType::ZX48K);
constexpr MachineTiming tnx = machine_timing(MachineType::ZXN_ISSUE2);
}  // namespace
static_assert(t48.master_cycles_per_line == static_cast<uint64_t>(t48.tstates_per_line) * 8,
              "a T-state is 8 master cycles");
static_assert(tnx.master_cycles_per_line == static_cast<uint64_t>(tnx.tstates_per_line) * 8, "");
static_assert(t48.master_cycles_per_frame == static_cast<uint64_t>(t48.tstates_per_frame) * 8, "");
static_assert(tnx.master_cycles_per_frame == static_cast<uint64_t>(tnx.tstates_per_frame) * 8, "");
// Same domain as Time's two fields, which is the point of the naming.
static_assert(std::is_same<decltype(MachineInfo::master_cycles_per_line),
                           decltype(Time::master_cycle)>::value, "");
static_assert(std::has_virtual_destructor<Listener>::value, "");
static_assert(std::has_virtual_destructor<Service>::value, "");

// The facade is a fixed collaborator of one `Emulator`, held for the process
// lifetime across a CTL-12 `Hard` reconstruct. Copying or moving it would
// duplicate the client table and the subscriptions.
static_assert(!std::is_copy_constructible<Debugger>::value, "");
static_assert(!std::is_move_constructible<Debugger>::value, "");

// `Condition` observes and `Handler` may mutate — the §4.2a / §4.3 split, in the
// signatures rather than only in the prose.
static_assert(std::is_same<Condition,
                           std::function<bool(const Event&, const Debugger&)>>::value,
              "a Condition observes: const Debugger&");
static_assert(std::is_same<Handler,
                           std::function<Action(const Event&, Debugger&)>>::value,
              "a Handler may mutate and returns a verdict that overrides the "
              "static action");

// CLIENT_NONE / EVENT_NONE are the "no such thing" values every default member
// initializer uses, so they must be zero.
static_assert(CLIENT_NONE == 0 && EVENT_NONE == 0, "");

}  // namespace
}  // namespace dbg
}  // namespace jnext
