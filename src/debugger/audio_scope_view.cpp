#include "debugger/audio_scope_view.h"
#include "debug/audio_math.h"
#include "debug/debugger.h"

#include <QGridLayout>
#include <QPainter>
#include <QPalette>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using jnext::dbg::AudioSource;
using jnext::dbg::AudioSourceScale;
using jnext::dbg::AUDIO_SOURCE_COUNT;

namespace {

const char* source_title(size_t i) {
    static const char* const kTitle[AUDIO_SOURCE_COUNT] = {
        "AY0 A", "AY0 B", "AY0 C", "AY1 A", "AY1 B", "AY1 C", "AY2 A", "AY2 B", "AY2 C",
        "DAC A", "DAC B", "DAC C", "DAC D", "Beeper", "Left", "Right"};
    return kTitle[i];
}

}  // namespace

/// One trace: the drawn window against the source's range, with its resting level as a faint line.
class ScopeTrace : public QWidget {
public:
    ScopeTrace(const AudioSourceScale& scale, QWidget* parent) : QWidget(parent), scale_(scale) {
        setMinimumSize(160, 34);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void set_samples(const std::vector<int16_t>* s) { s_ = s; update(); }
    QSize sizeHint() const override { return QSize(300, 34); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x10, 0x14, 0x10));
        const double span = std::max(1, scale_.hi - scale_.lo);
        auto y_of = [&](double v) { return (height() - 1) * (1.0 - (v - scale_.lo) / span); };
        p.setPen(QColor(0x40, 0x50, 0x40));
        p.drawLine(0, static_cast<int>(y_of(scale_.rest)), width(), static_cast<int>(y_of(scale_.rest)));
        if (!s_ || s_->size() < 2) return;
        p.setPen(QColor(0x30, 0xE0, 0x50));
        const double xs = static_cast<double>(width() - 1) / static_cast<double>(AudioScopeView::WINDOW - 1);
        QPointF prev(0, y_of((*s_)[0]));
        for (size_t i = 1; i < s_->size(); ++i) {
            const QPointF cur(static_cast<double>(i) * xs, y_of((*s_)[i]));
            p.drawLine(prev, cur);
            prev = cur;
        }
    }

private:
    AudioSourceScale scale_;
    const std::vector<int16_t>* s_ = nullptr;
};

AudioScopeView::AudioScopeView(jnext::dbg::Debugger* dbg, QWidget* parent)
    : QScrollArea(parent), dbg_(dbg) {
    setObjectName("audio_scope_view");
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(2, 2, 2, 2);
    outer->setSpacing(4);

    off_label_ = new QLabel(tr("Audio synthesis is off (--silent): the scopes need it."), page);
    off_label_->setObjectName("audio_scope_off");
    off_label_->setAlignment(Qt::AlignCenter);
    off_label_->setWordWrap(true);
    off_label_->hide();
    outer->addWidget(off_label_);

    grid_ = new QWidget(page);
    auto* grid = new QGridLayout(grid_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(3);
    grid->setColumnStretch(1, 1);
    QFont mono("Monospace", 8);
    mono.setStyleHint(QFont::Monospace);
    for (size_t i = 0; i < AUDIO_SOURCE_COUNT; ++i) {
        const int row = static_cast<int>(i);
        const AudioSourceScale sc = jnext::dbg::audio_source_scale(static_cast<AudioSource>(i));

        auto* left = new QWidget(grid_);
        auto* ll = new QVBoxLayout(left);
        ll->setContentsMargins(0, 0, 0, 0);
        ll->setSpacing(0);
        auto* title = new QLabel(tr(source_title(i)), left);
        title->setObjectName(QString("scope_title_%1").arg(i));
        title->setFont(mono);
        lamp_[i] = new QLabel(tr("free"), left);
        lamp_[i]->setObjectName(QString("scope_trig_%1").arg(i));
        lamp_[i]->setFont(mono);
        ll->addWidget(title);
        ll->addWidget(lamp_[i]);
        grid->addWidget(left, row, 0);

        trace_[i] = new ScopeTrace(sc, grid_);
        trace_[i]->setObjectName(QString("scope_trace_%1").arg(i));
        trace_[i]->setProperty("title", QString::fromLatin1(source_title(i)));
        grid->addWidget(trace_[i], row, 1);

        auto* right = new QWidget(grid_);
        auto* rl = new QVBoxLayout(right);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(0);
        meter_[i] = new QProgressBar(right);
        meter_[i]->setObjectName(QString("scope_meter_%1").arg(i));
        meter_[i]->setRange(0, 100);
        meter_[i]->setTextVisible(false);
        meter_[i]->setFixedHeight(8);
        meter_text_[i] = new QLabel(tr("pk 0  rms 0"), right);
        meter_text_[i]->setObjectName(QString("scope_meter_text_%1").arg(i));
        meter_text_[i]->setFont(mono);
        rl->addWidget(meter_[i]);
        rl->addWidget(meter_text_[i]);
        grid->addWidget(right, row, 2);
    }
    outer->addWidget(grid_);
    outer->addStretch(1);
    setWidget(page);
}

void AudioScopeView::refresh() {
    if (!dbg_) return;
    // The mixer does not run under --silent: say so, and do not look for data.
    const bool synthesis = dbg_->audio_state().host.synthesis_on;
    off_label_->setVisible(!synthesis);
    grid_->setVisible(synthesis);
    if (!synthesis) return;

    const auto r = dbg_->audio_scope_samples(2 * WINDOW);
    if (!r) return;   // not requested yet: the manager asks once the tab is on show
    for (size_t i = 0; i < AUDIO_SOURCE_COUNT; ++i) {
        const std::vector<int16_t>& v = r.value.samples[i];
        const AudioSourceScale sc = jnext::dbg::audio_source_scale(static_cast<AudioSource>(i));
        const ScopeTrigger t = scope_trigger(v.data(), v.size(), WINDOW);
        const size_t n = v.size() > t.start ? std::min(WINDOW, v.size() - t.start) : 0;
        shown_[i].assign(v.begin() + static_cast<long>(t.start), v.begin() + static_cast<long>(t.start + n));
        const ScopeMeter m = scope_meter(shown_[i].data(), shown_[i].size(), sc.rest);

        const int range = std::max(sc.hi - sc.rest, sc.rest - sc.lo);
        TraceState& st = state_[i];
        st.triggered = t.triggered;
        st.start = t.start;
        st.first_sample = r.value.first_sample;
        st.meter_percent = std::clamp(static_cast<int>(std::lround(100.0 * m.peak / range)), 0, 100);

        lamp_[i]->setText(t.triggered ? tr("trig") : tr("free"));
        meter_[i]->setValue(st.meter_percent);
        // Native units: the samples are x4 (the mixer's rounding gain).
        meter_text_[i]->setText(QString("pk %1  rms %2").arg(m.peak / 4).arg(std::lround(m.rms / 4.0)));
        trace_[i]->set_samples(&shown_[i]);
    }
}
