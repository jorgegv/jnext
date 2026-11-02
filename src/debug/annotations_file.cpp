#include "debug/annotations_file.h"

#include <istream>
#include <ostream>

#include "debug/long_address.h"

namespace jnext {
namespace dbg {

namespace {

constexpr const char* kHeader = "jnext-annotations 1";

std::string hex4(uint16_t a) {
    static const char* hex = "0123456789ABCDEF";
    std::string s = "$";
    for (int sh = 12; sh >= 0; sh -= 4) s += hex[(a >> sh) & 0xF];
    return s;
}

std::string escape(const std::string& t) {
    std::string r;
    for (char c : t) {
        if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else r += c;
    }
    return r;
}

// "" and sets `out` when `s` is `\\` / `\n` escapes only; else the reason.
std::string unescape(std::string_view s, std::string& out) {
    out.clear();
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { out.push_back(s[i]); continue; }
        if (i + 1 >= s.size()) return "the text ends in a lone backslash";
        const char n = s[++i];
        if (n == '\\') out.push_back('\\');
        else if (n == 'n') out.push_back('\n');
        else return std::string("unknown escape '\\") + n + "' in the text";
    }
    return "";
}

bool parse_addr(std::string_view f, uint16_t& a) {
    if (f.size() != 5 || f[0] != '$') return false;
    uint32_t v = 0;
    for (size_t i = 1; i < 5; ++i) {
        const char c = f[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return false;
        v = v * 16 + static_cast<uint32_t>(d);
    }
    a = static_cast<uint16_t>(v);
    return true;
}

bool blank(const std::string& l) {
    for (char c : l) if (c != ' ' && c != '\t') return false;
    return true;
}

}  // namespace

void write_annotations(std::ostream& out, const UserAnnotations& a) {
    out << kHeader << '\n';
    for (const auto& e : a.entries()) {
        const std::string head = space_tag(e.first.space) + " " + hex4(e.first.addr);
        if (!e.second.label.empty()) out << "label " << head << ' ' << e.second.label << '\n';
        if (!e.second.comment.empty())
            out << "comment " << head << ' ' << escape(e.second.comment) << '\n';
    }
}

bool read_annotations(std::istream& in, UserAnnotations& out, std::string& why) {
    UserAnnotations scratch;
    std::string line;
    int n = 0;
    bool header = false;
    auto fail = [&](const std::string& r) {
        why = "line " + std::to_string(n) + ": " + r;
        return false;
    };
    while (std::getline(in, line)) {
        ++n;
        if (n == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || blank(line)) continue;
        if (!header) {
            if (line == kHeader) { header = true; continue; }
            static const std::string pre = "jnext-annotations ";
            if (line.compare(0, pre.size(), pre) == 0) {
                const std::string v = line.substr(pre.size());
                bool digits = !v.empty() && v[0] != '0';
                for (char c : v) if (c < '0' || c > '9') digits = false;
                if (digits) return fail("version " + v + ": this jnext reads version 1");
            }
            return fail("not a jnext annotations file");
        }
        // A record: kind, memory, address, then the rest; one space between fields.
        std::string_view rest = line, kind, tag, adr;
        auto field = [&rest](std::string_view& tok) {
            const size_t sp = rest.find(' ');
            if (sp == std::string_view::npos) { tok = rest; rest = {}; return false; }
            tok = rest.substr(0, sp);
            rest.remove_prefix(sp + 1);
            return true;
        };
        const bool k_ok = field(kind);
        const bool is_label   = kind == "label";
        const bool is_comment = kind == "comment";
        if (!is_label && !is_comment)
            return fail("unknown record '" + std::string(kind) + "'");
        const std::string shape = is_label ? "label <SPACE> $<ADDR> <NAME>"
                                           : "comment <SPACE> $<ADDR> <TEXT>";
        if (!k_ok || !field(tag) || !field(adr)) return fail("a record is " + shape);
        const std::optional<MemSpace> sp = parse_space_tag(tag);
        if (!sp) return fail("unknown memory '" + std::string(tag) + "'");
        if (sp->kind == MemSpace::Kind::Cpu)
            return fail("memory CPU: a user entry names a memory");
        uint16_t addr = 0;
        if (!parse_addr(adr, addr))
            return fail("bad address '" + std::string(adr) + "' (want $ and 4 hex digits)");
        if (is_label) {
            if (rest.empty() || rest.find(' ') != std::string_view::npos)
                return fail("a label record has exactly <SPACE> $<ADDR> <NAME>");
            if (scratch.find(*sp, addr) && !scratch.find(*sp, addr)->label.empty())
                return fail("a second label for the same address and memory");
            const std::string name(rest);
            if (!UserAnnotations::valid_label_name(name))
                return fail("'" + name + "' is not a label name");
            const std::string e = scratch.set_label(*sp, addr, name);
            if (!e.empty()) return fail(e);
        } else {
            if (rest.empty()) return fail("a comment record needs its text");
            if (scratch.find(*sp, addr) && !scratch.find(*sp, addr)->comment.empty())
                return fail("a second comment for the same address and memory");
            if (rest.find('\r') != std::string_view::npos)
                return fail("a carriage return inside the text");
            std::string text;
            const std::string e = unescape(rest, text);
            if (!e.empty()) return fail(e);
            if (text.empty()) return fail("a comment record needs its text");
            const std::string e2 = scratch.set_comment(*sp, addr, text);
            if (!e2.empty()) return fail(e2);
        }
    }
    if (!header) {
        n = 1;
        return fail("not a jnext annotations file");
    }
    out = std::move(scratch);
    return true;
}

}  // namespace dbg
}  // namespace jnext
