// Every unit-test row reports its ID here — once per row the suite counts into
// its `Total:` line.
//
// test/run-unit-tests.sh points JNEXT_TEST_ROW_IDS at a per-suite file and FAILS
// a suite whose run reports one ID more than once, or reports a different number
// of IDs than its `Total:` says it ran. An ID is a global name in this project
// (the traceability matrix, the plan docs and every report key on it), so two
// assertions under one ID are one row in the matrix and two in the count — the
// manufactured coverage GH #190 was. The source text cannot answer this (an ID
// legitimately appears twice in a table, a message, or both arms of an `if`);
// only the run can.
//
// The ID-count half is what makes the gate honest: a row helper that does not
// call this still counts its row, so the suite reports fewer IDs than rows and
// is refused — an unwired helper cannot pass by reporting nothing.
//
// The ID is a literal, like the ID argument of every row helper: there is no
// std::string overload, because a row ID assembled at run time is one no source
// reader can see.
//
// Unset (a direct run of the binary): a no-op.
#pragma once

#include <cstdio>
#include <cstdlib>

inline void report_row_id(const char* id) {
    // APPEND, never "w": the harness creates the file empty before the suite
    // starts, and a process that opens it must not truncate it. A suite that
    // fork()s before its first report opens it twice — once per process —
    // and a truncating open would drop the other process's rows
    // (test/row_id_fork_probe.cpp, harness self-test HS-62).
    static std::FILE* const out = [] {
        const char* path = std::getenv("JNEXT_TEST_ROW_IDS");
        return (path && *path) ? std::fopen(path, "a") : nullptr;
    }();
    if (!out) return;
    std::fprintf(out, "%s\n", id ? id : "(null)");
    // Flushed per row, so nothing is buffered across a suite's fork(): a child
    // that exits through exit() would otherwise write the parent's rows twice.
    std::fflush(out);
}
