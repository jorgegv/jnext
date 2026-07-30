// sdfile_tool — put files into / get files out of a FAT32 SD-card image.
//
// A test-support binary, not a test suite: it registers no ctest entry and
// reports no rows. `nextsync-func` uses it to inject NextSync's dot command and
// its config file into the run's private SD clone before booting, and to read
// the synced files back out afterwards so the row can checksum them.
//
// It is a thin shell over `src/core/fat32_image.h` — the same
// read_tree / tree_upsert / format_and_populate sequence
// `sdcard_provisioner.cpp` already uses to inject MACHINES/NEXT/config.ini.
// That is deliberate: the project has an in-tree FAT32 writer, so a regression
// row does NOT need mtools or any other external tool to mutate an image.
//
// `put` takes every (path, file) pair in ONE invocation on purpose:
// fat32_format_and_populate reformats the partition and re-emits the whole
// tree, so N separate invocations would pay that cost N times.
//
//   sdfile_tool put <image> <path-in-image> <host-file> [<path> <file> ...]
//   sdfile_tool put-str <image> <path-in-image> <literal>
//   sdfile_tool get <image> <path-in-image> <host-file>
//
// Paths inside the image are forward-slash separated and matched
// case-insensitively (fat32_tree_upsert's own contract). Exit 0 on success,
// 1 on any failure with a reason on stderr.

#include "core/fat32_image.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int fail(const std::string& why) {
    std::fprintf(stderr, "sdfile_tool: %s\n", why.c_str());
    return 1;
}

bool read_host_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Total sector count of the first FAT32 partition, from its MBR entry. The
// partition table is the authority here rather than the BPB, because
// fat32_format_and_populate reformats in place and must be told the exact
// extent it may use.
bool partition_extent(const std::string& image, uint32_t& lba, uint32_t& sectors) {
    if (!fat32_find_partition(image, lba)) return false;
    std::ifstream f(image, std::ios::binary);
    if (!f) return false;
    uint8_t mbr[512];
    f.read(reinterpret_cast<char*>(mbr), sizeof mbr);
    if (!f) return false;
    for (int i = 0; i < 4; ++i) {
        const uint8_t* e = mbr + 446 + i * 16;
        uint32_t start = uint32_t(e[8]) | (uint32_t(e[9]) << 8) | (uint32_t(e[10]) << 16) |
                         (uint32_t(e[11]) << 24);
        uint32_t count = uint32_t(e[12]) | (uint32_t(e[13]) << 8) | (uint32_t(e[14]) << 16) |
                         (uint32_t(e[15]) << 24);
        if (start == lba && count != 0) {
            sectors = count;
            return true;
        }
    }
    return false;
}

const Fat32Node* find_path(const std::vector<Fat32Node>& level, const std::string& path) {
    const size_t slash = path.find('/');
    const std::string head = path.substr(0, slash);
    for (const Fat32Node& n : level) {
        if (head.size() != n.name.size()) continue;
        bool same = true;
        for (size_t i = 0; i < head.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(head[i])) !=
                std::tolower(static_cast<unsigned char>(n.name[i]))) {
                same = false;
                break;
            }
        }
        if (!same) continue;
        if (slash == std::string::npos) return n.is_dir ? nullptr : &n;
        return n.is_dir ? find_path(n.children, path.substr(slash + 1)) : nullptr;
    }
    return nullptr;
}

int do_put(const std::string& image, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& items) {
    uint32_t lba = 0, sectors = 0;
    if (!partition_extent(image, lba, sectors))
        return fail("no FAT32 partition found in '" + image + "'");

    Fat32Tree tree;
    if (!fat32_read_tree(image, lba, tree)) return fail("cannot read the directory tree");

    for (const auto& it : items) fat32_tree_upsert(tree, it.first, it.second);

    std::string err;
    if (!fat32_format_and_populate(image, lba, sectors, tree, err))
        return fail("write failed: " + err);
    return 0;
}

int do_get(const std::string& image, const std::string& path, const std::string& dst) {
    uint32_t lba = 0;
    if (!fat32_find_partition(image, lba)) return fail("no FAT32 partition in '" + image + "'");
    Fat32Tree tree;
    if (!fat32_read_tree(image, lba, tree)) return fail("cannot read the directory tree");
    const Fat32Node* n = find_path(tree.root, path);
    if (!n) return fail("'" + path + "' not found in the image");
    std::ofstream f(dst, std::ios::binary);
    if (!f) return fail("cannot open '" + dst + "' for writing");
    if (!n->data.empty()) f.write(reinterpret_cast<const char*>(n->data.data()),
                                  static_cast<std::streamsize>(n->data.size()));
    return f ? 0 : fail("write to '" + dst + "' failed");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: sdfile_tool put     <image> <path> <file> [<path> <file> ...]\n"
                     "       sdfile_tool put-str <image> <path> <literal>\n"
                     "       sdfile_tool get     <image> <path> <file>\n");
        return 1;
    }
    const std::string cmd = argv[1];
    const std::string image = argv[2];

    if (cmd == "put") {
        if (argc < 5 || ((argc - 3) % 2) != 0)
            return fail("put needs one or more <path> <file> pairs");
        std::vector<std::pair<std::string, std::vector<uint8_t>>> items;
        for (int i = 3; i + 1 < argc; i += 2) {
            std::vector<uint8_t> data;
            if (!read_host_file(argv[i + 1], data))
                return fail(std::string("cannot read host file '") + argv[i + 1] + "'");
            items.emplace_back(argv[i], std::move(data));
        }
        return do_put(image, items);
    }
    if (cmd == "put-str") {
        if (argc != 5) return fail("put-str needs <path> <literal>");
        const std::string s = argv[4];
        return do_put(image, {{argv[3], std::vector<uint8_t>(s.begin(), s.end())}});
    }
    if (cmd == "get") {
        if (argc != 5) return fail("get needs <path> <file>");
        return do_get(image, argv[3], argv[4]);
    }
    return fail("unknown command '" + cmd + "'");
}
