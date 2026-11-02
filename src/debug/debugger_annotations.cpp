// ---------------------------------------------------------------------------
// jnext::dbg::Debugger -- CAP-ANN, the user's labels and comments (GH #387,
// E23-24; design 10.2, 10.3, 10.5, 6.8).
//
// Two layers, one reader. `toolchain_symbols` is what a load put there;
// `annotations` is the user's store; `names` is the composition every reader
// goes through (`symbols()`, `lookup()`, `lookup_name()`): the user labels are
// added first and the toolchain layer merged after them, so by address and by
// name the user layer wins. `recompose_names()` runs after every change of
// either layer.
//
// These verbs touch no machine state, so they are allowed while the machine
// runs (like `load_map`), take no generation, and leave the rewind ring, the
// `.jns` stream and the attachment cache alone. They DO change what a panel
// shows, so each success bumps `state_revision` (GH #428).
// ---------------------------------------------------------------------------

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "debug/annotations_file.h"
#include "debug/debugger_impl.h"
#include "debug/long_address.h"
#include "debug/source_file.h"

namespace jnext {
namespace dbg {

void Debugger::Impl::recompose_names() {
    names = SymbolTable{};
    for (const auto& e : annotations.entries())
        if (!e.second.label.empty())
            names.add_label(e.second.label, e.first.addr, e.first.space);
    names.merge(toolchain_symbols);
}

namespace {

void say(std::string* why, const std::string& s) {
    if (why) *why = s;
}

}  // namespace

// ---------------------------------------------------------------------------

Result Debugger::set_user_label(FetchSource space, uint16_t addr, const std::string& name,
                                std::string* why) {
    impl_->recheck();
    // The name rule against the toolchain layer (D2): the name may be taken only at this
    // address and, when it is a paged toolchain name, in this memory.
    if (const auto t = impl_->toolchain_symbols.find_name(name)) {
        if (!(t->addr == addr && (!t->space || *t->space == space))) {
            say(why, "'" + name + "' already names " +
                     long_address(t->addr, t->space.value_or(MemSpace::cpu()),
                                  AddressStyle::Friendly, false) +
                     " in the loaded symbols");
            return Result::Unsupported;
        }
    }
    const std::string e = impl_->annotations.set_label(space, addr, name);
    if (!e.empty()) {
        say(why, e);
        return Result::Unsupported;
    }
    ++impl_->annotations_revision;
    impl_->recompose_names();
    impl_->bump_revision();   // GH #428: a panel shows the name now
    return Result::Ok;
}

Result Debugger::remove_user_label(FetchSource space, uint16_t addr) {
    impl_->recheck();
    if (!impl_->annotations.remove_label(space, addr)) return Result::RefusedUnavailable;
    ++impl_->annotations_revision;
    impl_->recompose_names();
    impl_->bump_revision();
    return Result::Ok;
}

Result Debugger::set_user_comment(FetchSource space, uint16_t addr, const std::string& text,
                                  std::string* why) {
    impl_->recheck();
    const std::string e = impl_->annotations.set_comment(space, addr, text);
    if (!e.empty()) {
        say(why, e);
        return Result::Unsupported;
    }
    ++impl_->annotations_revision;
    impl_->bump_revision();
    return Result::Ok;
}

const UserAnnotations& Debugger::annotations() const { return impl_->annotations; }

uint64_t Debugger::annotations_revision() const { return impl_->annotations_revision; }

const SymbolTable& Debugger::toolchain_symbols() const {
    impl_->recheck();
    return impl_->toolchain_symbols;
}

Result Debugger::replace_annotations(UserAnnotations a) {
    impl_->recheck();
    impl_->annotations = std::move(a);
    ++impl_->annotations_revision;
    impl_->recompose_names();
    impl_->bump_revision();
    return Result::Ok;
}

Result Debugger::load_annotations(const std::string& path, std::string* why) {
    const std::string problem = source_file_problem(path);
    if (!problem.empty()) {
        say(why, problem);
        return Result::RefusedUnavailable;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        say(why, "cannot open the file");
        return Result::RefusedUnavailable;
    }
    UserAnnotations parsed;
    std::string e;
    if (!read_annotations(in, parsed, e)) {
        say(why, e);
        return Result::Unsupported;
    }
    return replace_annotations(std::move(parsed));
}

Result Debugger::save_annotations(const std::string& path, std::string* why) const {
    const std::string part = path + ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (out) write_annotations(out, impl_->annotations);
        out.flush();
        if (!out) {
            std::error_code ec;
            std::filesystem::remove(part, ec);
            say(why, "cannot write '" + part + "'");
            return Result::RefusedUnavailable;
        }
    }
    std::error_code ec;
    std::filesystem::rename(part, path, ec);
    if (ec) {
        // Windows (MinGW libstdc++) does not replace an existing destination: remove it, retry.
        std::error_code rm;
        std::filesystem::remove(path, rm);
        ec.clear();
        std::filesystem::rename(part, path, ec);
    }
    if (ec) {
        std::filesystem::remove(part, ec);
        say(why, "cannot write '" + path + "'");
        return Result::RefusedUnavailable;
    }
    return Result::Ok;
}

}  // namespace dbg
}  // namespace jnext
