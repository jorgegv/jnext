#pragma once

#include "core/sdcard_provisioner.h"   // StallWatch

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class QApplication;
class QProgressDialog;

/// Task 27 GUI provisioning helper.
///
/// Provisioning runs at startup — possibly BEFORE the main window (and its
/// QApplication) exist. Qt permits at most one QApplication at a time, and a
/// QWidget needs one alive. This helper owns a SINGLE temporary QApplication
/// (created lazily on first use, only if none exists yet) that spans BOTH the
/// download-confirm prompt AND the download progress dialog, so we never
/// create/destroy two QApplications mid-provisioning.
///
/// Construct one helper, wire `confirm()` as the ConfirmFn and `progress()` as
/// the ProgressFn, run provisioning, then DESTROY the helper before QtApp
/// constructs its own QApplication (the destructor tears down the temporary
/// one, if it created it).
class SdcardGuiProvisioner {
public:
    // The clock the "Stalled" notice counts with: steady_clock::now() in
    // production, a fake in the tests.
    using Now = std::function<sdcard::StallWatch::Clock::time_point()>;

    SdcardGuiProvisioner();
    explicit SdcardGuiProvisioner(Now now);
    ~SdcardGuiProvisioner();

    SdcardGuiProvisioner(const SdcardGuiProvisioner&) = delete;
    SdcardGuiProvisioner& operator=(const SdcardGuiProvisioner&) = delete;

    // ConfirmFn seam: modal Yes/No. Returns true if the user accepts.
    bool confirm(const std::string& message);

    // ProgressFn seam: shows/updates a modal QProgressDialog with a bar.
    // Returns false when the user cancels (which aborts the download). The
    // backends call it at least every tick whatever the network does (the
    // contract in core/sdcard_provisioner.h), and each call pumps the event loop,
    // so the window stays alive through a stall. When no byte has arrived for
    // kStallNoticeMs the label also says "Stalled: no data received for N s", the
    // seconds counting up, until bytes come again.
    bool progress(uint64_t downloaded, uint64_t total);

    // BusyFn seam: run `work` (the multi-second copy+FAT32-patch) on a worker
    // thread while a modal, indeterminate "<phase>…" progress dialog animates
    // on the GUI thread — so the user sees jnext is busy, not hung. Not
    // cancellable (aborting mid-patch would leave a corrupt image). Returns
    // whatever `work` returns.
    bool busy(const std::string& phase, const std::function<bool()>& work);

    // The same dialogs serve the NextPi download, which needs its
    // own wording; the defaults are the SD card's.
    void set_texts(const std::string& title, const std::string& headline,
                   const std::string& busy_title, const std::string& download_label);

    // A modal warning: something the user asked for could not be done, and
    // the session goes on without it.
    void warn(const std::string& message);

private:
    void ensure_app();

    Now now_;
    std::unique_ptr<QApplication>    temp_app_;
    std::unique_ptr<QProgressDialog> dialog_;
    sdcard::StallWatch               watch_;       // started when the dialog is created
    std::string                      shown_note_;  // the stall note in the label now
    bool cancelled_ = false;
    std::string title_      = "jnext - SD card image";
    std::string headline_   = "No ZX Spectrum Next SD-card image found";
    std::string busy_title_ = "jnext - Preparing SD card image";
    std::string download_label_ = "Downloading NextZXOS distribution image…";
};
