#include "debugger/capability_gate.h"

#include <QEvent>
#include <QPalette>
#include <vector>

#include "debug/debugger.h"

namespace jnext {
namespace dbggate {

const Gate& gate_of(Surface s) { return kGates[static_cast<size_t>(s)]; }

std::optional<Capability> first_missing(Surface s, CapabilitySet caps) {
    const Gate& g = gate_of(s);
    for (int i = 0; i < g.n; ++i)
        if (!caps.has(g.needs[i])) return g.needs[i];
    return std::nullopt;
}

CapabilityView CapabilityView::of(const jnext::dbg::Debugger& dbg) {
    CapabilityView v;
    v.caps = dbg.capabilities();
    v.validity = dbg.register_validity();
    v.info = dbg.target_info();
    for (size_t i = 0; i < jnext::dbg::CAPABILITY_COUNT; ++i) {
        const Capability c = static_cast<Capability>(i);
        if (!v.caps.has(c)) v.why[i] = dbg.why_unavailable(c);
    }
    return v;
}

bool CapabilityView::operator==(const CapabilityView& o) const {
    return caps == o.caps && validity.bits == o.validity.bits && info.kind == o.info.kind &&
           info.name == o.info.name && info.version == o.info.version && info.link == o.info.link &&
           why == o.why;
}

QString display_text(std::string_view text, int max_chars) {
    // fromUtf8 turns every invalid sequence into U+FFFD and never fails.
    const QString raw = QString::fromUtf8(text.data(), static_cast<int>(text.size()));
    const auto cps = raw.toUcs4();
    std::vector<uint> out;
    bool pending_space = false;
    for (uint c : cps) {
        const QChar::Category cat = QChar::category(c);
        // whitespace, including the C0 ones that are control characters (\t \n \r \v \f)
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0B || c == 0x0C ||
                           cat == QChar::Separator_Space || cat == QChar::Separator_Line ||
                           cat == QChar::Separator_Paragraph;
        if (space) {
            pending_space = !out.empty();
            continue;
        }
        // C0 (NUL included), DEL, C1 are category Cc; bidi and zero-width characters are Cf
        if (cat == QChar::Other_Control || cat == QChar::Other_Format) continue;
        if (pending_space) out.push_back(' ');
        pending_space = false;
        out.push_back(c);
    }
    if (max_chars > 0 && out.size() > static_cast<size_t>(max_chars)) {
        out.resize(static_cast<size_t>(max_chars) - 1);
        out.push_back(0x2026);
    }
    QString res;
    for (uint c : out) {
        if (QChar::requiresSurrogates(c)) {
            res.append(QChar(QChar::highSurrogate(c)));
            res.append(QChar(QChar::lowSurrogate(c)));
        } else {
            res.append(QChar(static_cast<ushort>(c)));
        }
    }
    return res;
}

QString target_name(const CapabilityView& v) {
    const QString n = display_text(v.info.name, kNameChars);
    return n.isEmpty() ? QStringLiteral("remote target") : n;
}

QString unavailable_text(const CapabilityView& v, Capability c) {
    QString why = display_text(v.why[static_cast<size_t>(c) < v.why.size() ? static_cast<size_t>(c) : 0],
                               kReasonChars);
    if (why.isEmpty()) why = QStringLiteral("not available on this target");
    return QStringLiteral("Not available on %1: %2").arg(target_name(v), why);
}

QString unavailable_tooltip(const CapabilityView& v, Capability c) {
    return QStringLiteral("<qt>") + unavailable_text(v, c).toHtmlEscaped() + QStringLiteral("</qt>");
}

PanelPlaceholder::PanelPlaceholder(QWidget* panel) : QLabel(panel) {
    setObjectName(QStringLiteral("capability_placeholder"));
    setTextFormat(Qt::PlainText);
    setAlignment(Qt::AlignCenter);
    setWordWrap(true);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Window);
    panel->installEventFilter(this);
    hide();
}

void PanelPlaceholder::show_reason(const QString& text) {
    setText(text);
    setGeometry(parentWidget()->rect());
    active_ = true;
    show();
    raise();
}

void PanelPlaceholder::hide_reason() {
    active_ = false;
    hide();
}

bool PanelPlaceholder::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parent() && event->type() == QEvent::Resize && active_)
        setGeometry(parentWidget()->rect());
    return QLabel::eventFilter(watched, event);
}

}  // namespace dbggate
}  // namespace jnext
