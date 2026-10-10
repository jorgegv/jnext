// --sdcard-file-add: the row that needs a CASE-SENSITIVE host filesystem
// (GH #214).
//
// SDFA-T26 puts `Game.nex` and `GAME.NEX` side by side in one host directory and
// expects the copy to be refused as one name on the card. On a case-insensitive
// volume (APFS and NTFS by default) the second file overwrites the first, there
// is nothing to refuse, and -- worse -- the "refused" copy then succeeds and
// leaves its files on the card for every row after it. So the row lives in a
// suite the manifest declares `# os: linux`, ID and assertion unchanged.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "sdcard_file_add_includes.h"

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

void test_tree_case_sensitive() {
    const fs::path img = g_scratch / "refuse-case.img";
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
    const uint64_t pristine = file_digest(img);
    (void)pristine;
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
        const fs::path d = fresh("cases");
        write_host_file(d / "sub" / "Game.nex", payload(3, 114));
        write_host_file(d / "sub" / "GAME.NEX", payload(3, 115));
        refused("SDFA-T26", "two names differing only in case are refused, card untouched",
                FileAddStatus::DestInvalid, d, "would be the same name on the card");
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
    g_scratch = base / ("jnext-sdfa-linux-" + std::to_string(static_cast<unsigned long long>(stamp)));
    fs::create_directories(g_scratch, ec);
    if (ec) {
        std::printf("FATAL: cannot create scratch directory %s (%s)\n",
                    g_scratch.string().c_str(), ec.message().c_str());
        return 2;
    }

    test_tree_case_sensitive();

    fs::remove_all(g_scratch, ec);

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
