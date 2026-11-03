#pragma once

#include <QWidget>
#include <QLabel>

#include "debug/long_address.h"

namespace jnext { namespace dbg { class Debugger; } }

/// Debugger panel showing MMU slot-to-page mapping and 128K banking info.
/// Compact vertical layout for use alongside the CPU registers panel.
class MmuPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend every read goes through (GH #278 WP4a);
    ///             null shows nothing.
    explicit MmuPanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    void refresh();

    /// GH #360 - Friendly (`P0E`, `DIVROM`) or Physical (`$060000`) in the Memory column.
    void set_address_style(jnext::dbg::AddressStyle st) { style_ = st; }

private:
    void create_ui();

    const jnext::dbg::Debugger* dbg_;

    QLabel* slot_page_[8] = {};
    QLabel* slot_type_[8] = {};
    QLabel* slot_mem_[8] = {};   // GH #360: the memory a read of the slot reaches now
    jnext::dbg::AddressStyle style_ = jnext::dbg::AddressStyle::Friendly;
    QLabel* bank_128k_ = nullptr;
    QLabel* rom_select_ = nullptr;
    QLabel* paging_locked_ = nullptr;
};
