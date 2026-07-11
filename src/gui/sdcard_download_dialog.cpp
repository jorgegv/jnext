#include "gui/sdcard_download_dialog.h"

#include <QApplication>
#include <QMessageBox>
#include <QProgressDialog>
#include <QString>
#include <Qt>

SdcardGuiProvisioner::SdcardGuiProvisioner() = default;

SdcardGuiProvisioner::~SdcardGuiProvisioner() {
    // Destroy the QWidget before the QApplication that owns the event loop.
    dialog_.reset();
    if (temp_app_) {
        temp_app_->processEvents(); // flush any pending close events
        temp_app_.reset();
    }
}

void SdcardGuiProvisioner::ensure_app() {
    // Only spin up a temporary QApplication if none exists yet (this runs
    // before QtApp::init() creates the real one). Reuse an existing instance.
    if (QApplication::instance() == nullptr && !temp_app_) {
        static int argc = 1;
        static char arg0[] = "jnext";
        static char* argv[] = {arg0, nullptr};
        temp_app_ = std::make_unique<QApplication>(argc, argv);
    }
}

bool SdcardGuiProvisioner::confirm(const std::string& message) {
    ensure_app();

    QMessageBox box;
    box.setWindowTitle(QStringLiteral("jnext — SD card image"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("No ZX Spectrum Next SD-card image was found."));
    box.setInformativeText(QString::fromStdString(message));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    return box.exec() == QMessageBox::Yes;
}

bool SdcardGuiProvisioner::progress(uint64_t downloaded, uint64_t total) {
    ensure_app();

    if (cancelled_) return false; // already cancelled: keep aborting

    if (!dialog_) {
        dialog_ = std::make_unique<QProgressDialog>(
            QStringLiteral("Downloading NextZXOS distribution image…"),
            QStringLiteral("Cancel"), 0, 0);
        dialog_->setWindowTitle(QStringLiteral("jnext — Downloading"));
        dialog_->setWindowModality(Qt::ApplicationModal);
        dialog_->setMinimumDuration(0);   // show immediately
        dialog_->setAutoClose(false);
        dialog_->setAutoReset(false);
        dialog_->show();
    }

    if (total > 0) {
        // QProgressDialog is int-based; scale to a fixed 0..10000 range so we
        // do not overflow on multi-GB totals.
        dialog_->setMaximum(10000);
        const int value = static_cast<int>((downloaded * 10000ULL) / total);
        dialog_->setValue(value);
    } else {
        // No Content-Length → busy/indeterminate bar (min == max == 0).
        dialog_->setMaximum(0);
        dialog_->setValue(0);
    }

    // Repaint the bar during the synchronous libcurl transfer.
    QApplication::processEvents();

    if (dialog_->wasCanceled()) {
        cancelled_ = true;
        return false; // ProgressFn false → curl aborts the transfer
    }
    return true;
}
