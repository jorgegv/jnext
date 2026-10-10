// --sdcard-file-add: the rows that need POSIX facilities (GH #214).
//
// These rows used to sit in sdcard_file_add_test.cpp. Their fixtures need
// symbolic links, FIFOs, /dev/null or a file whose permission bits really deny
// the reader -- none of which Windows (or wine) provides -- so they live in a
// suite the manifest declares `# os: posix`, with IDs and assertions unchanged.
// The remaining rows of the feature run on every OS in sdcard_file_add_test.
// Oracle: the tree-copy and refusal contract at the top of
// src/core/sdcard_file_add.h and sdcard_file_add_test.cpp.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "sdcard_file_add_includes.h"
#include <sys/stat.h>   // mkfifo, chmod
#include <unistd.h>       // geteuid, syscall
#include "../dac_caps_dropped.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

void skip(const char* id, const char* desc, const std::string& why) {
    report_row_id(id);
    ++g_total;
    ++g_skip;
    std::printf("  SKIP %s: %s [%s]\n", id, desc, why.c_str());
}

}  // namespace

#include "sdcard_file_add_helpers.h"

namespace {

// SDFA-W50: a symlink to the image names the same image.
void test_writes_posix() {
    const fs::path img = g_scratch / "writes-posix.img";
    std::string why;
    if (!make_fixture(img, why)) {
        std::printf("FATAL: cannot build fixture image (%s)\n", why.c_str());
        std::exit(2);
    }
    std::error_code ec;
    {
        const fs::path link = g_scratch / "card-link.img";
        fs::remove(link, ec);
        std::error_code link_ec;
        fs::create_symlink(img, link, link_ec);
        check("SDFA-W50", "a symlink to the image names the same image",
              !link_ec && sdcard::same_image_file(img.string(), link.string()),
              link_ec ? "cannot create a symlink here: " + link_ec.message() : "");
        fs::remove(link, ec);
    }
}

// SDFA-D06, D12, D15: a symlink's name, a FIFO and a device as the source.
void test_defaults_posix() {
    std::string dest, err, why;
    // Lexical: the name TYPED, not the link's target.
    {
        const fs::path target = g_scratch / "v3.nex";
        const fs::path link   = g_scratch / "latest.nex";
        write_host_file(target, payload(10, 1));
        std::error_code ec;
        fs::remove(link, ec);
        fs::create_symlink(target, link, ec);
        check("SDFA-D06", "a symlink lands under the link's name, not its target's",
              !ec && sdcard::default_dest_path(link.string(), dest, err) &&
              dest == "/latest.nex", ec ? ec.message() : dest + err);
    }

    const fs::path img = g_scratch / "defaults-posix.img";
    if (!make_fixture(img, why)) {
        std::printf("FATAL: cannot build fixture image (%s)\n", why.c_str());
        std::exit(2);
    }
    FileAddStatus st = FileAddStatus::Ok;
    const uint64_t before = file_digest(img);
    // A FIFO given directly is refused before it is opened: opening one for
    // reading blocks until a writer appears, which in a script is forever.
    const fs::path fifo = g_scratch / "top.fifo";
    std::error_code ec;
    fs::remove(fifo, ec);
    const bool made = ::mkfifo(fifo.c_str(), 0600) == 0;
    st = made ? sdcard::add_to_image(img.string(), fifo.string(), "/F.BIN", false, err)
              : FileAddStatus::Ok;
    check("SDFA-D12", "a FIFO given as the source is refused without opening it",
          made && st == FileAddStatus::SourceUnreadable &&
          err.find("FIFO") != std::string::npos && file_digest(img) == before,
          made ? err : "mkfifo failed");
    fs::remove(fifo, ec);
    // A device is refused the same way: /dev/null would otherwise be read as
    // an empty file.
    st = sdcard::add_to_image(img.string(), "/dev/null", "/NULL.BIN", false, err);
    check("SDFA-D15", "a device given as the source is refused, card untouched",
          st == FileAddStatus::SourceUnreadable &&
          err.find("device") != std::string::npos && file_digest(img) == before, err);
}

// SDFA-T20..T24, T29, T30, T41, T42: links followed or refused, a FIFO, files
// the user cannot read.
void test_tree_posix() {
    const fs::path img = g_scratch / "refuse-posix.img";
    std::string why, err;
    if (!make_fixture(img, why)) {
        std::printf("FATAL: cannot build fixture image (%s)\n", why.c_str());
        std::exit(2);
    }
    std::error_code ec;
    auto fresh = [&](const char* name) {
        const fs::path d = g_scratch / name;
        fs::remove_all(d, ec);
        fs::create_directories(d / "sub", ec);
        // A valid file BEFORE the bad entry in name order, so a writer that
        // checked as it went would already have written it.
        write_host_file(d / "a-first.bin", payload(100, 110));
        return d;
    };

    // Symlinks are FOLLOWED: to a file, to a directory outside the tree.
    {
        const fs::path d = fresh("links");
        const fs::path outside = g_scratch / "outside";
        fs::create_directories(outside, ec);
        write_host_file(outside / "far.bin", payload(77, 111));
        write_host_file(g_scratch / "target.bin", payload(88, 112));
        std::error_code e1, e2;
        fs::create_symlink(g_scratch / "target.bin", d / "filelink.bin", e1);
        fs::create_directory_symlink(outside, d / "dirlink", e2);
        const FileAddStatus st =
            sdcard::add_to_image(img.string(), d.string(), "/LINKS", false, err);
        check("SDFA-T20", "a symlink to a file is followed: the card gets the bytes",
              !e1 && st == FileAddStatus::Ok &&
              card_file_is(img, "/LINKS/filelink.bin", payload(88, 112)), err);
        check("SDFA-T21", "a symlink to a directory is followed: the card gets its tree",
              !e2 && card_file_is(img, "/LINKS/dirlink/far.bin", payload(77, 111)), err);
    }
    // A link to a directory ALREADY copied — a sibling, not an ancestor — is
    // not a loop: `cp -rL` copies it twice, and so does this. The loop check
    // must forget a directory once its scan is finished.
    {
        const fs::path d = g_scratch / "dag";
        fs::remove_all(d, ec);
        fs::create_directories(d / "a-shared", ec);
        fs::create_directories(d / "b", ec);
        write_host_file(d / "a-shared" / "s.bin", payload(44, 117));
        std::error_code e1;
        fs::create_directory_symlink(d / "a-shared", d / "b" / "again", e1);
        const FileAddStatus st =
            sdcard::add_to_image(img.string(), d.string(), "/DAG", false, err);
        check("SDFA-T41", "a link to an already-copied sibling directory is copied, not refused",
              !e1 && st == FileAddStatus::Ok &&
              card_file_is(img, "/DAG/a-shared/s.bin", payload(44, 117)) &&
              card_file_is(img, "/DAG/b/again/s.bin", payload(44, 117)), err);
    }
    // The SOURCE itself a link to a directory: copied as that directory's
    // tree, under the LINK's name.
    {
        const fs::path real = g_scratch / "realdir";
        const fs::path link = g_scratch / "linkdir";
        fs::remove_all(real, ec);
        fs::create_directories(real, ec);
        write_host_file(real / "f.bin", payload(55, 118));
        fs::remove(link, ec);
        std::error_code e1;
        fs::create_directory_symlink(real, link, e1);
        sdcard::AddSummary sum;
        const FileAddStatus st =
            sdcard::add_to_image(img.string(), link.string(), "", false, err, &sum);
        check("SDFA-T42", "a source that is a link to a directory is copied as a tree, by the link's name",
              !e1 && st == FileAddStatus::Ok && sum.is_dir &&
              card_file_is(img, "/linkdir/f.bin", payload(55, 118)) &&
              !card_has(img, "/realdir"), err);
    }
    const uint64_t base = file_digest(img);
    // Every refusal starts from this card, not from whatever the previous row
    // left: one row that fails by WRITING must not fail every row after it.
    const fs::path base_img = g_scratch / "refuse-base.img";
    fs::copy_file(img, base_img, fs::copy_options::overwrite_existing, ec);

    auto refused = [&](const char* id, const char* desc, FileAddStatus want,
                       const fs::path& d, const char* must_say) {
        std::error_code restore_ec;
        fs::copy_file(base_img, img, fs::copy_options::overwrite_existing, restore_ec);
        const FileAddStatus st =
            sdcard::add_to_image(img.string(), d.string(), "/BAD", false, err);
        check(id, desc,
              st == want && err.find(must_say) != std::string::npos &&
              file_digest(img) == base && !card_has(img, "/BAD"), err);
    };
    {
        const fs::path d = fresh("loop");
        fs::create_directory_symlink(d, d / "sub" / "loop", ec);
        refused("SDFA-T22", "a symlink back into its own ancestry is refused, card untouched",
                FileAddStatus::SourceUnreadable, d, "never end");
    }
    {
        const fs::path d = fresh("dangle");
        fs::create_symlink(g_scratch / "no-such-target", d / "sub" / "gone.bin", ec);
        refused("SDFA-T23", "a dangling symlink is refused, card untouched",
                FileAddStatus::SourceUnreadable, d, "symbolic link to nothing");
    }
    {
        const fs::path d = fresh("fifo");
        const bool made = ::mkfifo((d / "sub" / "pipe").c_str(), 0600) == 0;
        if (made)
            refused("SDFA-T24", "a FIFO inside the tree is refused, card untouched",
                    FileAddStatus::SourceUnreadable, d, "FIFO");
        else
            check("SDFA-T24", "a FIFO inside the tree is refused, card untouched",
                  false, "mkfifo failed");
    }
    // Unreadable. Root reads through permission bits (CI runs in a container
    // as root), so as root the two DAC capabilities are dropped from the
    // effective set for these rows; see DacCapsDropped.
    DacCapsDropped caps;
    if (::geteuid() == 0 && !caps.drop()) {
        skip("SDFA-T29", "an unreadable file in the tree is refused, card untouched",
             "running as root and capset refused dropping CAP_DAC_OVERRIDE");
        skip("SDFA-T30", "an unreadable directory in the tree is refused, card untouched",
             "running as root and capset refused dropping CAP_DAC_OVERRIDE");
    } else {
        const fs::path d = fresh("unread");
        write_host_file(d / "sub" / "secret.bin", payload(3, 116));
        ::chmod((d / "sub" / "secret.bin").c_str(), 0);
        refused("SDFA-T29", "an unreadable file in the tree is refused, card untouched",
                FileAddStatus::SourceUnreadable, d, "cannot open source file");
        ::chmod((d / "sub" / "secret.bin").c_str(), 0600);

        const fs::path d2 = fresh("unlist");
        ::chmod((d2 / "sub").c_str(), 0);
        refused("SDFA-T30", "an unreadable directory in the tree is refused, card untouched",
                FileAddStatus::SourceUnreadable, d2, "cannot list directory");
        ::chmod((d2 / "sub").c_str(), 0700);
    }
}

}  // namespace

int main() {
    std::error_code ec;
    const char* env_tmp = std::getenv("TMPDIR");
    const fs::path base = env_tmp && *env_tmp ? fs::path(env_tmp)
                                              : fs::temp_directory_path();
    // Unique per run so two concurrent invocations (or a leftover directory
    // from a killed one) never share fixtures.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    g_scratch = base / ("jnext-sdfa-posix-" + std::to_string(static_cast<unsigned long long>(stamp)));
    fs::create_directories(g_scratch, ec);
    if (ec) {
        std::printf("FATAL: cannot create scratch directory %s (%s)\n",
                    g_scratch.string().c_str(), ec.message().c_str());
        return 2;
    }

    test_writes_posix();
    test_defaults_posix();
    test_tree_posix();

    fs::remove_all(g_scratch, ec);

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
