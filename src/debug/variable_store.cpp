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
                             std::vector<std::string> refused)
    : variables_(std::move(variables)), types_(std::move(types)),
      modules_(std::move(modules)), refused_(std::move(refused))
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
}

}  // namespace dbg
}  // namespace jnext
