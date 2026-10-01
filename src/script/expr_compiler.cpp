// jnext::script — compile_expr / eval_expr. See expr_compiler.h.

#include "script/expr_compiler.h"

#include <memory>
#include <utility>

#include "debug/debugger.h"
#include "script/check.h"
#include "script/evaluator.h"
#include "script/parser.h"

namespace jnext {
namespace script {

SymbolResolver symbols_of(const dbg::Debugger& dbg) {
    const dbg::Debugger* d = &dbg;
    return [d](const std::string& name) { return d->lookup_name(name); };
}

namespace {

/// Parse + bind `text` for `scope`. Null with `errors` filled on failure.
ExprPtr compile_tree(const std::string& text, const PayloadScope& scope,
                     const SymbolResolver& symbols, std::vector<Diagnostic>& errors) {
    if (scope.tag == PayloadScope::Tag::ScriptStop) {
        errors.push_back(Diagnostic{SourcePos{}, "a `stop` rule's condition is not a backend "
                                                 "predicate: only the script engine evaluates it"});
        return nullptr;
    }
    if (scope.tag == PayloadScope::Tag::Event && scope.kind == dbg::EventKind::Count) {
        errors.push_back(Diagnostic{SourcePos{}, "invalid event kind"});
        return nullptr;
    }
    ExprParseResult pr = parse_expression(text);
    if (!pr.ok()) {
        errors.push_back(*pr.error);
        return nullptr;
    }
    bind_expr(*pr.expr, scope, /*vars=*/nullptr, symbols, /*require_symbols=*/true, errors);
    if (!errors.empty()) return nullptr;
    return pr.expr;
}

}  // namespace

CompiledPredicate compile_expr(const std::string& text, PayloadScope scope,
                               const CompileOptions& opts) {
    CompiledPredicate out;
    ExprPtr tree = compile_tree(text, scope, opts.symbols, out.errors);
    if (!tree) return out;
    // The event is read only when the scope has one; in the `None` scope no
    // payload name can have been bound, so the delivered Event is ignored.
    const bool uses_event = scope.tag == PayloadScope::Tag::Event;
    RuntimeErrorHandler on_error = opts.on_runtime_error;
    std::shared_ptr<const Expr> root = std::move(tree);
    out.predicate = [root, on_error, uses_event](const dbg::Event& ev,
                                                const dbg::Debugger& dbg) -> bool {
        try {
            EvalContext ctx{dbg, uses_event ? &ev : nullptr};
            return eval_int(*root, ctx) != 0;
        } catch (const EvalError& err) {
            if (on_error) on_error(err.d);
            return false;
        }
    };
    return out;
}

EvalResult eval_expr(const std::string& text, const dbg::Debugger& dbg) {
    EvalResult out;
    ExprPtr tree = compile_tree(text, PayloadScope::none(), symbols_of(dbg), out.errors);
    if (!tree) return out;
    try {
        EvalContext ctx{dbg, nullptr};
        out.value = eval_int(*tree, ctx);
        out.ok    = true;
    } catch (const EvalError& err) {
        out.errors.push_back(err.d);
    }
    return out;
}

}  // namespace script
}  // namespace jnext
