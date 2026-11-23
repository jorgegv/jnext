#include "gui/startup_error_box.h"

#include "core/log.h"
#include "gui/desktop_color_scheme.h"

#include <QApplication>
#include <QMessageBox>
#include <QtGlobal>
#include <memory>

namespace gui {

void show_startup_error(const std::string& title, const std::string& text)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // No display and no platform plugin asked for: QApplication would abort the process. The
    // error is on stderr already, and the exit status must stay the plain failure it was.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("DISPLAY") &&
        qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        Log::platform()->debug("dialog not shown, no display: {}: {}", title, text);
        return;
    }
#endif
    static std::unique_ptr<QApplication> temp_app;
    if (QApplication::instance() == nullptr && !temp_app) {
        static int argc = 1;
        static char arg0[] = "jnext";
        static char* argv[] = {arg0, nullptr};
        temp_app = std::make_unique<QApplication>(argc, argv);
        DesktopColorScheme::install();
    }
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        Log::platform()->debug("dialog not shown on the offscreen platform: {}: {}", title, text);
        return;
    }
    QMessageBox box(QMessageBox::Critical, QString::fromStdString(title),
                    QString::fromStdString(text), QMessageBox::Ok);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.exec();
}

}  // namespace gui
