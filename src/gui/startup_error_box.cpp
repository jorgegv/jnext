#include "gui/startup_error_box.h"

#include "core/log.h"
#include "gui/desktop_color_scheme.h"

#include <QApplication>
#include <QMessageBox>
#include <memory>

namespace gui {

void show_startup_error(const std::string& title, const std::string& text)
{
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
