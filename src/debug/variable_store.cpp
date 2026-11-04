#include "debug/variable_store.h"

#include <algorithm>
#include <numeric>
#include <utility>

namespace jnext {
namespace dbg {

namespace {

template <typename T>
bool by_text(const T& a, const T& b)
{
    return a.text != b.text ? a.text < b.text : a.module < b.module;
}

}  // namespace

VariableStore::VariableStore(std::vector<CFunction> functions, std::vector<CVariable> variables,
                             std::vector<CRecord> types, std::vector<CRecord> modules,
                             std::vector<std::string> refused, CFrameHelpers helpers,
                             CNoRecordFacts facts)
    : variables_(std::move(variables)), types_(std::move(types)),
      modules_(std::move(modules)), refused_(std::move(refused)),
      helpers_(std::move(helpers)), facts_(facts)
{
    std::vector<size_t> order(functions.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return by_text(functions[a], functions[b]);
    });
    std::vector<int> new_index(functions.size());
    functions_.reserve(functions.size());
    for (size_t i = 0; i < order.size(); ++i) {
        new_index[order[i]] = static_cast<int>(i);
        functions_.push_back(std::move(functions[order[i]]));
    }
    for (CVariable& v : variables_)
        if (v.function >= 0) v.function = new_index[static_cast<size_t>(v.function)];
    std::sort(variables_.begin(), variables_.end(), by_text<CVariable>);
    std::sort(types_.begin(), types_.end(), by_text<CRecord>);
    std::sort(modules_.begin(), modules_.end(), by_text<CRecord>);
    std::sort(refused_.begin(), refused_.end());
    for (CFunction& f : functions_)
        std::sort(f.scopes.begin(), f.scopes.end(),
                  [](const CScope& a, const CScope& b) { return a.offset < b.offset; });
}

std::optional<CLocation> VariableStore::locate(const SourceAddress& at) const
{
    for (size_t i = 0; i < functions_.size(); ++i)
        for (const CPiece& p : functions_[i].pieces)
            if (p.at.space == at.space && at.address >= p.at.address && at.address < p.hi_excl)
                return CLocation{static_cast<int>(i), p.offset + (at.address - p.at.address)};
    return std::nullopt;
}

const CScope* VariableStore::scope_at(int function, uint32_t offset) const
{
    if (function < 0 || static_cast<size_t>(function) >= functions_.size()) return nullptr;
    const CScope* found = nullptr;
    for (const CScope& s : functions_[static_cast<size_t>(function)].scopes) {
        if (s.offset > offset) break;
        found = &s;
    }
    return found;
}

}  // namespace dbg
}  // namespace jnext
