#pragma once

#include <QWidget>

#include <optional>
#include <vector>

#include "debug/long_address.h"
#include "debug/mem_space.h"

class QLabel;
class QLineEdit;
class QTableWidget;
namespace jnext { namespace dbg { class Debugger; } }

/// GH #366 - the Symbols tab: every label the backend knows (`symbols().symbols()`, the toolchain's
/// symbols and the user's labels), filterable, with the memory each is assembled for. A label of
/// paged memory that is not what supplies its address now is greyed, and the panel never jumps to
/// it: the Disassembly shows the CPU's map, so jumping there would show other code under the
/// label's name.
///
/// The rows are rebuilt only when `state_revision()` moved (a map load, an annotation, any write),
/// so a step does not rebuild thousands of rows; the greying is redone on every refresh(), because
/// a page can be mapped without the revision moving.
class SymbolsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SymbolsPanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Rebuild when the backend's revision moved; re-grey on every call.
    void refresh();

    /// GH #360 - Friendly (`P20`) or Physical text in the Memory column.
    void set_address_style(jnext::dbg::AddressStyle st);

    /// Put the keyboard in the filter box (the Go menu's Symbols entry, and the tab's own focus).
    void focus_filter();

signals:
    void show_in_disassembly_requested(uint16_t addr);
    void show_in_memory_requested(uint16_t addr);
    void show_in_source_requested(uint16_t addr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Row {
        uint16_t addr = 0;
        std::optional<jnext::dbg::MemSpace> space;   // none: any memory
    };
    void rebuild_();
    void regrey_();
    void apply_filter_();
    bool mapped_(const Row& r) const;
    QString unmapped_text_(const Row& r) const;
    void activate_row_(int row);
    void show_menu_(int row, const QPoint& global);

    const jnext::dbg::Debugger* dbg_;
    QLineEdit*    filter_  = nullptr;
    QLabel*       count_   = nullptr;
    QLabel*       message_ = nullptr;
    QTableWidget* table_   = nullptr;
    std::vector<Row> rows_;
    uint64_t rev_ = ~0ull;
    jnext::dbg::AddressStyle style_ = jnext::dbg::AddressStyle::Friendly;
};
