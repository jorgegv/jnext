#pragma once

// ---------------------------------------------------------------------------
// The user's labels and comments (GH #387, E23-24; design 10.2, 10.5).
//
// A pure store: std headers and `mem_space.h` only (the .cpp also uses `long_address.h`, to
// refuse a memory the project file cannot name), no machine, no file. An
// entry is keyed by (memory, 16-bit logical address), the key of the page-aware
// symbol table (10.1). A user entry always names a memory (design 10.4), so
// `MemSpace::cpu()` is refused. At most one label and one comment per key; a
// name is used at one key only.
//
// The backend (`Debugger`, CAP-ANN) owns one instance beside the toolchain's
// `SymbolTable` and composes the two for readers; the project file is
// `annotations_file.h`.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "debug/mem_space.h"

namespace jnext {
namespace dbg {

/// The key of an entry. Ordered by (kind, index, address): the file's order.
struct AnnotationKey {
    MemSpace space;
    uint16_t addr = 0;
    bool operator<(const AnnotationKey& o) const {
        if (space.kind != o.space.kind) return space.kind < o.space.kind;
        if (space.index != o.space.index) return space.index < o.space.index;
        return addr < o.addr;
    }
    bool operator==(const AnnotationKey& o) const { return space == o.space && addr == o.addr; }
};

/// What a key holds; "" is none.
struct Annotation {
    std::string label;
    std::string comment;
    bool operator==(const Annotation& o) const { return label == o.label && comment == o.comment; }
};

class UserAnnotations {
public:
    /// The label grammar (design 10.5; the DSL's `@name` lexes exactly this):
    /// components `[A-Za-z_][A-Za-z0-9_]*` joined by single dots (`main.loop`).
    static bool valid_label_name(std::string_view name);

    /// Whether `text` is well-formed UTF-8 (no overlong form, no surrogate, nothing
    /// past U+10FFFF).
    static bool valid_utf8(std::string_view text);

    /// Set the label at a key (a rename when it has one). Returns "" when done, else
    /// why not, the store unchanged: the Cpu space, a name outside the grammar, a name
    /// already used at another key.
    std::string set_label(MemSpace space, uint16_t addr, const std::string& name);

    /// Remove the label at a key; false when there is none.
    bool remove_label(MemSpace space, uint16_t addr);

    /// Set the comment at a key; "" text removes it. CRLF and a lone CR become LF,
    /// any other text is kept byte for byte. Returns "" when done, else why not: the
    /// Cpu space, text that is not UTF-8.
    std::string set_comment(MemSpace space, uint16_t addr, std::string text);

    const Annotation* find(MemSpace space, uint16_t addr) const;
    std::optional<AnnotationKey> find_name(const std::string& name) const;

    /// Every entry, in file order.
    const std::map<AnnotationKey, Annotation>& entries() const { return entries_; }
    bool   empty() const { return entries_.empty(); }
    size_t label_count() const { return names_.size(); }
    size_t comment_count() const;
    bool operator==(const UserAnnotations& o) const { return entries_ == o.entries_; }
    bool operator!=(const UserAnnotations& o) const { return !(*this == o); }

private:
    std::map<AnnotationKey, Annotation> entries_;
    std::map<std::string, AnnotationKey> names_;   ///< name -> its key, one each
};

}  // namespace dbg
}  // namespace jnext
