// distro_variant: make a "next release" of a distribution image at run time (GH #555, the real-content
// tier of the upgrade tests). It reads the tree of a RAW distribution image, changes one text file,
// adds one and removes one, and writes the result as a new sparse image with the same partition
// geometry, so a test can zip it under another distribution name and upgrade to it. Test-support
// binary, never shipped; the images it reads and writes live in the calling row's scratch directory.
//
//   distro_variant RAW OUT [CHANGE_PATH ADD_PATH REMOVE_PATH]
//     defaults: /README.md  /VARIANT-ADDED.TXT  /CONTRIBUTING.md
//   The changed file becomes the text "variant release", the added file "added by the variant release".
#include "core/fat32_image.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> text(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::string up(std::string s) { for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32); return s; }

// Remove the node at `path` (case-insensitive, '/'-separated); true when it was there.
bool remove_node(std::vector<Fat32Node>& level, const std::string& path) {
    const size_t slash = path.find('/', 1);
    const std::string head = path.substr(1, slash == std::string::npos ? std::string::npos : slash - 1);
    for (size_t i = 0; i < level.size(); ++i) {
        if (up(level[i].name) != up(head)) continue;
        if (slash == std::string::npos) { level.erase(level.begin() + static_cast<long>(i)); return true; }
        return level[i].is_dir && remove_node(level[i].children, path.substr(slash));
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 6) {
        std::fprintf(stderr, "usage: distro_variant RAW OUT [CHANGE_PATH ADD_PATH REMOVE_PATH]\n");
        return 2;
    }
    const std::string raw = argv[1], out = argv[2];
    const std::string change = argc == 6 ? argv[3] : "/README.md";
    const std::string add    = argc == 6 ? argv[4] : "/VARIANT-ADDED.TXT";
    const std::string remove = argc == 6 ? argv[5] : "/CONTRIBUTING.md";

    uint32_t lba = 0;
    Fat32Tree tree;
    if (!fat32_find_partition(raw, lba) || !fat32_read_tree(raw, lba, tree)) {
        std::fprintf(stderr, "distro_variant: cannot read the tree of %s\n", raw.c_str());
        return 1;
    }
    std::ifstream in(raw, std::ios::binary);
    std::vector<uint8_t> mbr(512);
    in.read(reinterpret_cast<char*>(mbr.data()), 512);
    in.seekg(0, std::ios::end);
    const uint64_t bytes = static_cast<uint64_t>(in.tellg());
    const uint32_t sectors = static_cast<uint32_t>(mbr[0x1BE + 12] | mbr[0x1BE + 13] << 8 | mbr[0x1BE + 14] << 16 |
                                                   static_cast<uint32_t>(mbr[0x1BE + 15]) << 24);
    if (!in || bytes < 512 || sectors == 0) { std::fprintf(stderr, "distro_variant: %s has no usable MBR\n", raw.c_str()); return 1; }

    fat32_tree_upsert(tree, change, text("variant release\n"));
    fat32_tree_upsert(tree, add, text("added by the variant release\n"));
    if (!remove_node(tree.root, remove)) {
        std::fprintf(stderr, "distro_variant: %s is not on %s\n", remove.c_str(), raw.c_str());
        return 1;
    }
    {
        std::ofstream o(out, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(mbr.data()), 512);
        o.seekp(static_cast<std::streamoff>(bytes - 1));
        const char z = 0;
        o.write(&z, 1);
        if (!o) { std::fprintf(stderr, "distro_variant: cannot write %s\n", out.c_str()); return 1; }
    }
    std::string err;
    Fat32WriteOptions opts;
    opts.apply_meta = true;   // keep the attributes and write times of everything that was there
    if (!fat32_format_and_populate(out, lba, sectors, tree, err, opts)) {
        std::fprintf(stderr, "distro_variant: %s\n", err.c_str());
        return 1;
    }
    std::printf("wrote %s (%llu bytes): changed %s, added %s, removed %s\n", out.c_str(),
                static_cast<unsigned long long>(bytes), change.c_str(), add.c_str(), remove.c_str());
    return 0;
}
