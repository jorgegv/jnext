#pragma once

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
    SdcardGuiProvisioner();
    ~SdcardGuiProvisioner();

    SdcardGuiProvisioner(const SdcardGuiProvisioner&) = delete;
    SdcardGuiProvisioner& operator=(const SdcardGuiProvisioner&) = delete;

    // ConfirmFn seam: modal Yes/No. Returns true if the user accepts.
    bool confirm(const std::string& message);

    // ProgressFn seam: shows/updates a modal QProgressDialog with a bar.
    // Returns false when the user cancels (which aborts the download).
    bool progress(uint64_t downloaded, uint64_t total);

    // BusyFn seam: run `work` (the multi-second copy+FAT32-patch) on a worker
    // thread while a modal, indeterminate "<phase>…" progress dialog animates
    // on the GUI thread — so the user sees jnext is busy, not hung. Not
    // cancellable (aborting mid-patch would leave a corrupt image). Returns
    // whatever `work` returns.
    bool busy(const std::string& phase, const std::function<bool()>& work);

private:
    void ensure_app();

    std::unique_ptr<QApplication>    temp_app_;
    std::unique_ptr<QProgressDialog> dialog_;
    bool cancelled_ = false;
};
