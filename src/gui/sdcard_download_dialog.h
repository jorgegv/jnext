#pragma once

#include "core/sdcard_provisioner.h"   // StallWatch

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class QApplication;
class QMessageBox;
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

    // UpgradeFn seam: the offer of a newer NextZXOS distribution. A modal box with the
    // provisioner's text (it names both versions, says the user's files are carried over
    // and the old card is kept as a backup) and three buttons: "Upgrade (keep my files)",
    // "Keep my card (don't ask again)" and "Not now" (the default, and what closing or
    // Escape means). Upgrade is Replace, "don't ask again" is KeepDontAskAgain, anything
    // else Keep.
    sdcard::UpgradeChoice offer_upgrade(const std::string& from, const std::string& to,
                                        const std::string& message, bool files_carry_over = true);
    // The box offer_upgrade() runs, not yet shown, with its buttons handed back for the
    // caller (the tests) to read.
    struct UpgradeBox {
        std::unique_ptr<QMessageBox> box;
        class QAbstractButton*       replace  = nullptr;
        class QAbstractButton*       never    = nullptr;
        class QAbstractButton*       keep     = nullptr;
    };
    static UpgradeBox upgrade_box(const std::string& to, const std::string& message, bool files_carry_over = true);
    // The answer for the button the user pressed (nullptr = the box was closed).
    static sdcard::UpgradeChoice upgrade_answer(const UpgradeBox& b, const class QAbstractButton* pressed);
    // The report of a finished upgrade (what was carried over, what the new NextZXOS replaced):
    // a box with a summary and the whole list as its detailed text.
    void show_report(const std::string& report);
    static std::unique_ptr<QMessageBox> report_box(const std::string& report);

    // ProgressFn seam: shows/updates a modal QProgressDialog with a bar.
    // Returns false when the user cancels (which aborts the download). The
    // backends call it at least every tick whatever the network does (the
    // contract in core/sdcard_provisioner.h), and each call pumps the event loop,
    // so the window stays alive through a stall. When no byte has arrived for
    // kStallNoticeMs the label also says "Stalled: no data received for N s", the
    // seconds counting up, until bytes come again.
    bool progress(uint64_t downloaded, uint64_t total);

    // SleepFn seam (GH #497): the wait between two download attempts. Runs the event
    // loop for `ms`, so the dialog keeps repainting and its Cancel works (a click ends
    // the wait at once; the next progress() then returns false), and the label says
    // `note` ("Retry 1 of 3 in 2 s"). The next attempt gets a stall watch of its own.
    void retry_wait(unsigned ms, const std::string& note);

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
    // the session goes on without it. Its text can be selected with the mouse,
    // so an address in it can be copied (GH #385). On the offscreen platform no
    // dialog is shown (nobody could dismiss it); the title and the text go to the
    // platform log at debug level instead, so a regression row can see the wiring.
    void warn(const std::string& message);

    // The warning dialog warn() runs, not yet shown: a QMessageBox whose text is
    // selectable by mouse whatever the widget style (the "Windows" style's
    // default is links only, so the address in it could not be copied).
    static std::unique_ptr<QMessageBox> warning_box(const std::string& title, const std::string& message);

    // ShortFetchFn seam (GH #385): runs `fetch` (a release list, a checksum, handed
    // a ProgressFn) and returns what it returns. The session keeps answering (every
    // call of the ProgressFn pumps the event loop), but nothing is shown while the
    // fetch is quick: a modal progress dialog with `label` and a Cancel button
    // appears only once the fetch has taken kShortFetchShowMs, and says "Stalled"
    // like progress() does. It is gone when `fetch` returns. A Cancel ends only this
    // fetch: a later progress() (the archive download) is not cancelled by it.
    bool short_fetch(const std::string& label, const std::function<bool(const sdcard::ProgressFn&)>& fetch);

    // A fetch quicker than this shows no dialog at all: the usual sub-second fetch
    // must not flash one. The console's line waits as long (sdcard::kShortFetchShowMs
    // is the one definition, in the core).
    static constexpr int kShortFetchShowMs = sdcard::kShortFetchShowMs;

private:
    void ensure_app();
    // A download dialog as progress() and short_fetch() show it: modal, an empty
    // 0..10000 bar, a Cancel button, shown at once.
    static std::unique_ptr<QProgressDialog> make_download_dialog(const std::string& label);
    // The label and the bar of `dlg` for this call: the plain `label`, plus the stall
    // note once no byte has arrived for sdcard::kStallNoticeMs (`shown_note` is the
    // note in the label now); the bar only when the total is known.
    static void refresh_download_dialog(QProgressDialog& dlg, const std::string& label,
                                        sdcard::StallWatch& watch, std::string& shown_note,
                                        sdcard::StallWatch::Clock::time_point now,
                                        uint64_t downloaded, uint64_t total);

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
