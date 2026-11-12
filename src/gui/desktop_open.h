#pragma once

// Files the desktop asks JNext to open (doc/design/MACOS-FINDER-OPEN.md).
//
// On macOS the Finder does not put a double-clicked file on the command line:
// it launches (or activates) the app and sends an Apple Event, which Qt turns
// into a QFileOpenEvent sent to the QApplication. The same event carries a
// file from Open With, and from a drop on the Dock icon. Other platforms never
// send it, so installing the filter everywhere costs nothing and keeps it
// reachable by the Linux suites.
//
// The one decision, at_launch(), is whether the open is the reason JNext was
// started (load it, as the command line would) or arrives at an app that is
// already running (ask first: what is running would be lost). The launch
// open is delivered as the event loop starts, inside a fraction of the
// window; nothing the user cares about can exist yet in that time.

#include <QEvent>
#include <QFileOpenEvent>
#include <QObject>
#include <QString>

#include <chrono>
#include <functional>
#include <optional>
#include <utility>

namespace desktop_open {

/// How long after the event loop starts an open still counts as the launch.
inline constexpr qint64 kLaunchWindowMs = 3000;

/// `ms_since_loop_start` < 0 means the loop has not started: the launch.
constexpr bool at_launch(qint64 ms_since_loop_start)
{
    return ms_since_loop_start < kLaunchWindowMs;
}

/// Milliseconds from `loop_started` to `now`; -1 when the loop has not started.
inline qint64 ms_since_loop_start(const std::optional<std::chrono::steady_clock::time_point>& loop_started,
                                  std::chrono::steady_clock::time_point now)
{
    if (!loop_started) return -1;
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - *loop_started).count();
}

/// Installed on the QApplication: hands every QFileOpenEvent's file to
/// `handler` and consumes the event. Everything else passes through.
class FileOpenFilter : public QObject {
public:
    using Handler = std::function<void(const QString& path)>;

    explicit FileOpenFilter(Handler handler, QObject* parent = nullptr)
        : QObject(parent), handler_(std::move(handler)) {}

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const QString file = static_cast<QFileOpenEvent*>(event)->file();
            if (!file.isEmpty() && handler_) handler_(file);
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    Handler handler_;
};

}  // namespace desktop_open
