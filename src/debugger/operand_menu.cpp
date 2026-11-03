#include "debugger/operand_menu.h"

#include "debugger/breakpoint_model.h"
#include "debugger/watch_panel.h"

#include <QAction>
#include <QMenu>
#include <QString>

namespace {

QString hex4(unsigned v) { return QString::number(v, 16).rightJustified(4, QLatin1Char('0')); }

}  // namespace

void add_operand_actions(QMenu& menu, QObject* ctx, const MenuOperands& ops,
                         WatchPanel* watches, BreakpointModel* bps)
{
    // --- Watch entries ---
    const bool any_watch = watches && (!ops.labels.empty() || ops.symbol || ops.immediate ||
                                       !ops.regs.empty());
    if (any_watch) {
        menu.addSeparator();
        auto named = [&](const MenuOperands::Named& n) {
            auto* a = menu.addAction(QStringLiteral("Watch '%1' ($%2)")
                                         .arg(QString::fromStdString(n.first), hex4(n.second)));
            QObject::connect(a, &QAction::triggered, ctx, [watches, n]() {
                watches->add_watch(n.second, n.first);
            });
        };
        for (const auto& l : ops.labels) named(l);
        if (ops.symbol) named(*ops.symbol);
        if (ops.immediate) {
            const uint16_t imm = *ops.immediate;
            const std::string label = ops.immediate_label;
            auto* a = menu.addAction(QStringLiteral("Watch $%1").arg(hex4(imm)));
            QObject::connect(a, &QAction::triggered, ctx, [watches, imm, label]() {
                watches->add_watch(imm, label);
            });
        }
        for (const auto& rp : ops.regs) {
            auto* a = menu.addAction(QStringLiteral("Watch (%1) = $%2")
                                         .arg(QString::fromLatin1(rp.name), hex4(rp.value)));
            QObject::connect(a, &QAction::triggered, ctx, [watches, rp]() {
                watches->add_watch(rp.value, std::string("(") + rp.name + ")");
            });
        }
    }

    // --- Data breakpoint entries ---
    //
    // GH #218/#220 - each adds a DATA breakpoint, which the Breakpoints panel
    // lists; the model's mutators notify that list themselves, so there is no
    // repaint of our own here. Without a model the entries are inert.
    if (!(ops.symbol || ops.immediate || !ops.regs.empty())) return;
    menu.addSeparator();
    auto pair = [&](const QString& what, uint16_t addr) {
        auto* r = menu.addAction(QStringLiteral("Break on Read %1").arg(what));
        QObject::connect(r, &QAction::triggered, ctx, [bps, addr]() {
            if (bps) bps->add(BreakpointModel::Read, addr);
        });
        auto* w = menu.addAction(QStringLiteral("Break on Write %1").arg(what));
        QObject::connect(w, &QAction::triggered, ctx, [bps, addr]() {
            if (bps) bps->add(BreakpointModel::Write, addr);
        });
    };
    if (ops.symbol)
        pair(QStringLiteral("'%1' ($%2)").arg(QString::fromStdString(ops.symbol->first),
                                              hex4(ops.symbol->second)),
             ops.symbol->second);
    if (ops.immediate) pair(QStringLiteral("$%1").arg(hex4(*ops.immediate)), *ops.immediate);
    for (const auto& rp : ops.regs)
        pair(QStringLiteral("(%1) = $%2").arg(QString::fromLatin1(rp.name), hex4(rp.value)), rp.value);
}
