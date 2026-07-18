#include "gui/sdcard_download_dialog.h"

#include <QApplication>
#include <QEventLoop>
#include <QMessageBox>
#include <QProgressDialog>
#include <QString>
#include <Qt>

#include <atomic>
#include <chrono>
#include <thread>

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
    // Rich-text <b> forces bold on styles that don't embolden the main text.
    box.setText(QStringLiteral("<b>No ZX Spectrum Next SD-card image found</b>"));
    box.setInformativeText(QString::fromStdString(message));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    return box.exec() == QMessageBox::Yes;
}

bool SdcardGuiProvisioner::progress(uint64_t downloaded, uint64_t total) {
    ensure_app();

    if (cancelled_) return false; // already cancelled: keep aborting

    if (!dialog_) {
        // Fixed 0..10000 range (int-based; scaled so multi-GB totals don't
        // overflow). Starting with a real range + value 0 shows an EMPTY bar
        // on the first frame — a (0,0) "busy" range paints as a FULL bar in
        // several Qt styles until the first real total arrives.
        dialog_ = std::make_unique<QProgressDialog>(
            QStringLiteral("Downloading NextZXOS distribution image…"),
            QStringLiteral("Cancel"), 0, 10000);
        dialog_->setWindowTitle(QStringLiteral("jnext — Downloading"));
        dialog_->setWindowModality(Qt::ApplicationModal);
        dialog_->setMinimumDuration(0);   // show immediately
        dialog_->setAutoClose(false);
        dialog_->setAutoReset(false);
        dialog_->setValue(0);             // start empty, not full
        dialog_->show();
    }

    if (total > 0) {
        const int value = static_cast<int>((downloaded * 10000ULL) / total);
        dialog_->setValue(value);
    }
    // total == 0 (Content-Length not yet known): leave the bar at its current
    // value (0% at first) rather than switching to a busy bar, which some
    // styles paint as full.

    // Repaint the bar during the synchronous libcurl transfer.
    QApplication::processEvents();

    if (dialog_->wasCanceled()) {
        cancelled_ = true;
        return false; // ProgressFn false → curl aborts the transfer
    }
    return true;
}

bool SdcardGuiProvisioner::busy(const std::string& phase,
                                const std::function<bool()>& work) {
    ensure_app();

    // Replace the (now-complete) download bar with an indeterminate "busy"
    // dialog. Range (0,0) is Qt's standard busy indicator: an animated marquee
    // rather than a fillable bar, which is exactly the "something is happening"
    // cue we want during the copy+patch. No cancel button — aborting a
    // half-written FAT would corrupt the image.
    dialog_.reset();
    QProgressDialog dlg(QString::fromStdString(phase) + QStringLiteral("…"),
                        QString(), 0, 0);
    dlg.setWindowTitle(QStringLiteral("jnext — Preparing SD card image"));
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setCancelButton(nullptr);
    dlg.setMinimumDuration(0);   // show immediately
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    dlg.show();

    // Run the blocking work on a worker thread (filesystem only — no Qt), and
    // keep pumping the GUI event loop here so the marquee animates. The worker
    // touches no Qt objects, so this is thread-safe.
    std::atomic<bool> done{false};
    bool result = false;
    std::thread worker([&]() {
        result = work ? work() : true;
        done.store(true, std::memory_order_release);
    });
    while (!done.load(std::memory_order_acquire)) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    worker.join();

    dlg.close();
    QApplication::processEvents();
    return result;
}
