#include "debug/user_annotations.h"

#include "debug/long_address.h"

namespace jnext {
namespace dbg {

namespace {

bool ident_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool ident_char(char c) { return ident_start(c) || (c >= '0' && c <= '9'); }

std::string key_text(const AnnotationKey& k) {
    static const char* hex = "0123456789ABCDEF";
    std::string s = "$";
    for (int sh = 12; sh >= 0; sh -= 4) s += hex[(k.addr >> sh) & 0xF];
    return s + " " + space_tag(k.space);
}

}  // namespace

bool UserAnnotations::valid_label_name(std::string_view n) {
    if (n.empty() || !ident_start(n[0])) return false;
    for (size_t i = 1; i < n.size(); ++i) {
        if (ident_char(n[i])) continue;
        if (n[i] == '.' && i + 1 < n.size() && ident_start(n[i + 1])) continue;
        return false;
    }
    return true;
}

bool UserAnnotations::valid_utf8(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = 0;
        uint32_t cp = 0, min = 0;
        if (c < 0x80) { ++i; continue; }
        else if (c >= 0xC2 && c <= 0xDF) { n = 1; cp = c & 0x1F; min = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { n = 2; cp = c & 0x0F; min = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 0x07; min = 0x10000; }
        else return false;
        if (i + n >= s.size()) return false;
        for (size_t k = 1; k <= n; ++k) {
            const unsigned char d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += n + 1;
    }
    return true;
}

std::string UserAnnotations::set_label(MemSpace space, uint16_t addr, const std::string& name) {
    if (space.kind == MemSpace::Kind::Cpu)
        return "a user label names a memory, not the CPU view";
    if (!valid_label_name(name))
        return "'" + name + "' is not a label name (letters, digits and _, not starting "
               "with a digit; '.' joins parts)";
    const AnnotationKey key{space, addr};
    auto taken = names_.find(name);
    if (taken != names_.end()) {
        if (taken->second == key) return "";
        return "'" + name + "' already names " + key_text(taken->second);
    }
    Annotation& a = entries_[key];
    if (!a.label.empty()) names_.erase(a.label);
    a.label = name;
    names_[name] = key;
    return "";
}

bool UserAnnotations::remove_label(MemSpace space, uint16_t addr) {
    auto it = entries_.find(AnnotationKey{space, addr});
    if (it == entries_.end() || it->second.label.empty()) return false;
    names_.erase(it->second.label);
    it->second.label.clear();
    if (it->second.comment.empty()) entries_.erase(it);
    return true;
}

std::string UserAnnotations::set_comment(MemSpace space, uint16_t addr, std::string text) {
    if (space.kind == MemSpace::Kind::Cpu)
        return "a user comment names a memory, not the CPU view";
    if (!valid_utf8(text)) return "the comment is not valid UTF-8";
    std::string t;
    t.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            t.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            t.push_back(text[i]);
        }
    }
    const AnnotationKey key{space, addr};
    if (t.empty()) {
        auto it = entries_.find(key);
        if (it == entries_.end()) return "";
        it->second.comment.clear();
        if (it->second.label.empty()) entries_.erase(it);
        return "";
    }
    entries_[key].comment = std::move(t);
    return "";
}

const Annotation* UserAnnotations::find(MemSpace space, uint16_t addr) const {
    auto it = entries_.find(AnnotationKey{space, addr});
    return it == entries_.end() ? nullptr : &it->second;
}

std::optional<AnnotationKey> UserAnnotations::find_name(const std::string& name) const {
    auto it = names_.find(name);
    if (it == names_.end()) return std::nullopt;
    return it->second;
}

size_t UserAnnotations::comment_count() const {
    size_t n = 0;
    for (const auto& e : entries_) n += e.second.comment.empty() ? 0 : 1;
    return n;
}

}  // namespace dbg
}  // namespace jnext
