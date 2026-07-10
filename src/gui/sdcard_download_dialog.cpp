#include "gui/sdcard_download_dialog.h"

#include <QApplication>
#include <QMessageBox>

#include <memory>

bool sdcard_gui_confirm_download(const std::string& message) {
    // A QApplication must exist before any QWidget. This prompt runs before
    // QtApp::init() creates the real one, so spin up a temporary instance
    // when none exists yet; it is destroyed on return so QtApp can create
    // its own (Qt permits at most one QApplication at a time).
    std::unique_ptr<QApplication> temp;
    if (QApplication::instance() == nullptr) {
        static int argc = 1;
        static char arg0[] = "jnext";
        static char* argv[] = {arg0, nullptr};
        temp = std::make_unique<QApplication>(argc, argv);
    }

    QMessageBox box;
    box.setWindowTitle(QStringLiteral("jnext — SD card image"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("No ZX Spectrum Next SD-card image was found."));
    box.setInformativeText(QString::fromStdString(message));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    const int ret = box.exec();
    return ret == QMessageBox::Yes;
}
