#pragma once

// ---------------------------------------------------------------------------
// The one place that says which debugger-window surface needs which capability
// of the facade's target (epic #486 R1.3). A surface is a menu item, a button, a
// panel or a panel-internal control; `kGates` has one row per `Surface`, and
// `kNoSurface` names each `Capability` that has no surface in this window, with
// why. Together they name every `Capability` (test RTQ-01).
//
// No other file decides which capability a surface needs: the window and the
// panels ask `first_missing(Surface::X, caps)`.
//
// Everything a target answers is untrusted text (a network peer from the epic's
// R2 on): `display_text()` is the one defence, and a tooltip is forced rich text
// with escaped content so no markup a target sends is ever interpreted.
// ---------------------------------------------------------------------------

#include <QLabel>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "debug/target.h"

namespace jnext { namespace dbg { class Debugger; } }

namespace jnext {
namespace dbggate {

using jnext::dbg::Capability;
using jnext::dbg::CapabilitySet;

enum class Surface : uint8_t {
    Run,            ///< Debug > Run / Continue, toolbar Continue
    Pause,          ///< Debug > Pause / Break, toolbar Break
    Step,           ///< Single Step / Over / Out (menu, toolbar), Debug > Source forward steps, Source tab buttons
    RunToCursor,    ///< the Run to Cursor action, Disassembly and Source "Run to Here" and Enter
    ExecBp,         ///< Add Execute Breakpoint, Load from Script, the Execute dialog item, gutter adds
    WatchBp,        ///< Add Read / Write / Read/Write Breakpoint, the dialog items, the operand menu's Break on Read / Write
    EventBp,        ///< the Add dialog's IO / Interrupt / NMI / Audio items
    RunToInterrupt, ///< Debug > Run to Next Interrupt
    RegEdit,        ///< CPU panel register edit and flag click
    MemoryView,     ///< Add Watch, the operand menu's Watch entries; the Memory, Stack, Watches and Variables panels
    PhysMem,        ///< Memory panel Slot and Page views
    MmuSlots,       ///< MMU panel slot rows
    NextRegView,    ///< the NextREG panel
    SpritesView,    ///< the Sprites panel
    CycleExact,     ///< Run to End of Frame / Scan Line
    RewindCmd,      ///< Frame Back, Step Back, Rewind menu, slider, Jump Here, Source back / reverse
    TraceCmd,       ///< Debug > Trace, toolbar Trace and Export Trace
    CoverageCmd,    ///< Debug > Coverage
    CallStackView,  ///< Call Stack panel header
    ScriptsCmd,     ///< Script menu and key actions, the Script panel
    LoadCmd,        ///< File > Reload Last File
    VideoView,      ///< the Video and Graphics panels
    CopperView,     ///< the Copper panel
    AudioView,      ///< the Audio panel
    TimeField,      ///< CPU panel T-states
    PagingFields,   ///< MMU panel 128K bank / ROM / lock
    UlaScreenField, ///< CPU panel ULA screen
    FetchLabel,     ///< the Disassembly Mem column header
    Count
};

constexpr size_t SURFACE_COUNT = static_cast<size_t>(Surface::Count);

struct Gate {
    Surface     surface;
    const char* name;
    /// The window registers widgets for this surface (the rest live inside a panel).
    bool        in_window;
    int         n;                 ///< 1 or 2 needs
    Capability  needs[2];
};

constexpr Gate kGates[] = {
    {Surface::Run,            "run",              true,  1, {Capability::Run, Capability::Count}},
    {Surface::Pause,          "pause",            true,  1, {Capability::Pause, Capability::Count}},
    {Surface::Step,           "step",             true,  1, {Capability::InstructionStep, Capability::Count}},
    {Surface::RunToCursor,    "run-to-cursor",    true,  1, {Capability::RunToCursor, Capability::Count}},
    {Surface::ExecBp,         "execute-bp",       true,  1, {Capability::ExecuteBreakpoints, Capability::Count}},
    {Surface::WatchBp,        "watch-bp",         true,  1, {Capability::Watchpoints, Capability::Count}},
    {Surface::EventBp,        "event-bp",         false, 1, {Capability::MachineEvents, Capability::Count}},
    {Surface::RunToInterrupt, "run-to-interrupt", true,  2, {Capability::Run, Capability::MachineEvents}},
    {Surface::RegEdit,        "register-edit",    false, 1, {Capability::RegisterWrite, Capability::Count}},
    {Surface::MemoryView,     "memory",           true,  1, {Capability::Memory, Capability::Count}},
    {Surface::PhysMem,        "physical-memory",  false, 1, {Capability::PhysicalMemory, Capability::Count}},
    {Surface::MmuSlots,       "mmu-slots",        false, 1, {Capability::MmuSlots, Capability::Count}},
    {Surface::NextRegView,    "nextreg",          true,  1, {Capability::NextRegs, Capability::Count}},
    {Surface::SpritesView,    "sprites",          true,  1, {Capability::SpriteData, Capability::Count}},
    {Surface::CycleExact,     "cycle-exact",      true,  1, {Capability::CycleExact, Capability::Count}},
    {Surface::RewindCmd,      "rewind",           true,  1, {Capability::Rewind, Capability::Count}},
    {Surface::TraceCmd,       "trace",            true,  1, {Capability::Trace, Capability::Count}},
    {Surface::CoverageCmd,    "coverage",         true,  1, {Capability::Coverage, Capability::Count}},
    {Surface::CallStackView,  "call-stack",       false, 1, {Capability::CallStackTracked, Capability::Count}},
    {Surface::ScriptsCmd,     "scripts",          true,  1, {Capability::Scripts, Capability::Count}},
    {Surface::LoadCmd,        "load",             true,  1, {Capability::Load, Capability::Count}},
    {Surface::VideoView,      "video",            true,  1, {Capability::Video, Capability::Count}},
    {Surface::CopperView,     "copper",           true,  1, {Capability::Copper, Capability::Count}},
    {Surface::AudioView,      "audio",            true,  1, {Capability::Audio, Capability::Count}},
    {Surface::TimeField,      "t-states",         false, 1, {Capability::MachineTime, Capability::Count}},
    {Surface::PagingFields,   "paging-fields",    false, 1, {Capability::PagingLatches, Capability::Count}},
    {Surface::UlaScreenField, "ula-screen",       false, 2, {Capability::Video, Capability::PagingLatches}},
    {Surface::FetchLabel,     "fetch-label",      false, 1, {Capability::FetchOverlays, Capability::Count}},
};
static_assert(sizeof(kGates) / sizeof(kGates[0]) == SURFACE_COUNT, "kGates: one row per Surface");

/// A capability with no surface in the debugger window, and why.
struct NoSurface {
    Capability  cap;
    const char* why;
};

constexpr NoSurface kNoSurface[] = {
    {Capability::CommandsWhileRunning, "runtime behaviour of the target link, not a control"},
    {Capability::MemoryWhileRunning,   "runtime behaviour of the target link, not a control"},
    {Capability::Ports,                "the window has no port panel"},
    {Capability::SpritePaletteClip,    "the Sprites panel shows neither palette nor clip"},
    {Capability::CtcDma,               "no CTC or DMA panel"},
    {Capability::MagicBreakpoint,      "its action is in the emulator window, which is always local"},
    {Capability::Reset,                "its action is in the emulator window, which is always local"},
    {Capability::Input,                "its action is in the emulator window, which is always local"},
    {Capability::Capture,              "its action is in the emulator window, which is always local"},
    {Capability::MachineState,         "its action is in the emulator window, which is always local"},
};
constexpr size_t kNoSurfaceCount = sizeof(kNoSurface) / sizeof(kNoSurface[0]);

const Gate& gate_of(Surface s);

/// The capability whose reason is shown for `s` on `caps`: the first of its needs that is missing.
std::optional<Capability> first_missing(Surface s, CapabilitySet caps);

/// One snapshot of what a facade says about its target: the capability set, the register validity
/// mask, the identity and the reason of every missing capability. A default view is a local one: all
/// present.
struct CapabilityView {
    CapabilitySet              caps = CapabilitySet::all();
    jnext::dbg::RegisterValidity validity = jnext::dbg::RegisterValidity::all();
    jnext::dbg::TargetInfo     info;
    std::array<std::string, jnext::dbg::CAPABILITY_COUNT> why;   ///< filled only for a missing capability

    static CapabilityView of(const jnext::dbg::Debugger& dbg);

    bool has(Capability c) const { return caps.has(c); }
    bool valid(jnext::dbg::RegisterValidity::Field f) const { return validity.valid(f); }
    bool missing(Surface s) const { return first_missing(s, caps).has_value(); }

    bool operator==(const CapabilityView& o) const;
    bool operator!=(const CapabilityView& o) const { return !(*this == o); }
};

/// Text a target supplied, made safe to show: control characters, DEL, C1 and format characters
/// (bidi overrides, zero-width) dropped, whitespace runs folded to one space, invalid UTF-8 shown as
/// U+FFFD, cut at `max_chars` code points with an ellipsis.
QString display_text(std::string_view text, int max_chars);

constexpr int kNameChars   = 64;
constexpr int kReasonChars = 200;

/// The target's name for display ("remote target" when it gave none).
QString target_name(const CapabilityView& v);

/// "Not available on <name>: <reason>", plain text (a status line, a placeholder).
QString unavailable_text(const CapabilityView& v, Capability c);

/// The same as a tooltip: forced rich text with the content escaped.
QString unavailable_tooltip(const CapabilityView& v, Capability c);

/// A plain-text label laid over a panel while its capability is missing; the panel stays the dock's
/// widget and is not refreshed meanwhile.
class PanelPlaceholder : public QLabel {
    Q_OBJECT
public:
    explicit PanelPlaceholder(QWidget* panel);
    void show_reason(const QString& text);
    void hide_reason();
    bool active() const { return active_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool active_ = false;
};

}  // namespace dbggate
}  // namespace jnext
