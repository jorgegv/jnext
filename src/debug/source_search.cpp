#include "debug/source_search.h"

#include <filesystem>
#include <system_error>

namespace source_search {

namespace fs = std::filesystem;

namespace {

bool is_file(const fs::path& p)
{
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

/// The name's components with the root dropped, normalised, with no leading
/// ".." and no "." or empty component.
std::vector<std::string> components_of(const fs::path& name)
{
    std::vector<std::string> out;
    for (const fs::path& c : name.relative_path().lexically_normal()) {
        const std::string s = c.string();
        if (s.empty() || s == ".") continue;
        if (s == ".." && out.empty()) continue;
        out.push_back(s);
    }
    return out;
}

} // namespace

std::string resolve(const std::string& recorded, const std::string& map_file,
                    const std::vector<std::string>& dirs)
{
    if (recorded.empty()) return {};
    const fs::path source(recorded);
    if (source.is_absolute()) {
        if (is_file(source)) return source.string();
    } else if (!map_file.empty()) {
        fs::path root = fs::path(map_file).parent_path();
        while (!root.empty()) {
            const fs::path candidate = root / source;
            if (is_file(candidate)) return candidate.string();
            const fs::path parent = root.parent_path();
            if (parent == root) break;
            root = parent;
        }
    }

    const std::vector<std::string> parts = components_of(source);
    for (size_t keep = parts.size(); keep >= 1; --keep) {
        fs::path tail;
        for (size_t i = parts.size() - keep; i < parts.size(); ++i) tail /= parts[i];
        for (const std::string& dir : dirs) {
            if (dir.empty()) continue;
            const fs::path candidate = fs::path(dir) / tail;
            if (is_file(candidate)) return candidate.string();
        }
    }
    return {};
}

std::string root_for(const std::string& recorded, const std::string& picked)
{
    const std::vector<std::string> want = components_of(fs::path(recorded));
    std::vector<fs::path> have;
    for (const fs::path& c : fs::path(picked)) {
        if (!c.empty()) have.push_back(c);
    }
    size_t shared = 0;
    while (shared < want.size() && shared < have.size() &&
           want[want.size() - 1 - shared] == have[have.size() - 1 - shared].string())
        ++shared;
    if (shared == 0) return {};
    fs::path root;
    for (size_t i = 0; i + shared < have.size(); ++i) root /= have[i];
    return root.empty() ? "." : root.string();
}

std::vector<std::string> split_list(std::string_view text)
{
    std::vector<std::string> out;
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t sep = text.find(kListSeparator, begin);
        const size_t end = sep == std::string_view::npos ? text.size() : sep;
        if (end > begin) out.emplace_back(text.substr(begin, end - begin));
        if (sep == std::string_view::npos) break;
        begin = sep + 1;
    }
    return out;
}

std::string join_list(const std::vector<std::string>& dirs)
{
    std::string out;
    for (const std::string& d : dirs) {
        if (!out.empty()) out += kListSeparator;
        out += d;
    }
    return out;
}

} // namespace source_search
