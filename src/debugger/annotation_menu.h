#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <QString>

#include "debug/mem_space.h"

class QMenu;
class QWidget;
namespace jnext { namespace dbg { class Debugger; } }

/// One place an annotation entry of a code view acts on: a 16-bit address in the memory
/// that supplies it (GH #387, design 10.1). `text` names the place in a submenu.
struct AnnotationPlace {
    uint16_t addr = 0;
    jnext::dbg::MemSpace space = jnext::dbg::MemSpace::cpu();
    QString text;
};

/// Append the user-annotation entries (GH #387, E23-24) to a code view's context menu, behind
/// a separator: "Add Label..." / "Rename Label...", "Remove Label", "Add Comment..." /
/// "Edit Comment...". The Disassembly and the Source tab both call it, so they offer the same
/// words in the same order (as add_operand_actions() does, GH #423).
///  - one place: the entries act on it; several: each entry is a submenu listing the places by
///    their `text`; none: the entries are disabled, with `no_place_tip` as their tooltip.
///  - Rename on a toolchain label creates a user label that shadows it; Remove acts on user labels
///    only, so on a toolchain-only label it is disabled and says where the label comes from.
///  - A refused name or text is a warning with the backend's reason and changes nothing.
///  - `changed` is called after a successful change.
/// The actions are owned by `menu`; the dialogs are parented to `parent`.
void add_annotation_actions(QMenu& menu, QWidget* parent, jnext::dbg::Debugger& dbg,
                            const std::vector<AnnotationPlace>& places,
                            const std::function<void()>& changed,
                            const QString& no_place_tip = QString());
