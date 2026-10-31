#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class BreakpointModel;
class QMenu;
class QObject;
class WatchPanel;

/// What a code view knows about the operands of the line a context menu was
/// opened on. The Disassembly and the Source tab both fill one and call
/// add_operand_actions(), so the two views offer the same watch and data
/// breakpoint entries with the same words in the same order (GH #423).
struct MenuOperands {
    using Named = std::pair<std::string, uint16_t>;
    /// The names of the line's own address(es): "Watch 'name' ($addr)".
    std::vector<Named> labels;
    /// A named operand (the Source tab's word under the cursor): the watch entry
    /// and both data breakpoints.
    std::optional<Named> symbol;
    /// A numeric operand; `immediate_label` names it ("" for none).
    std::optional<uint16_t> immediate;
    std::string immediate_label;
    /// Register-indirect operands: "(HL)" and its value.
    struct Reg { const char* name; uint16_t value; };
    std::vector<Reg> regs;
};

/// Append the watch entries (only with a watch panel) and the data breakpoint
/// entries, each section after a separator, a section only when it has entries.
/// The actions are owned by `menu`; `ctx` is the connection context.
void add_operand_actions(QMenu& menu, QObject* ctx, const MenuOperands& ops,
                         WatchPanel* watches, BreakpointModel* bps);
