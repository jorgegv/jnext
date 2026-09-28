// A stand-in suite for test/harness-selftest.sh (HS-62), NOT a declared suite:
// it is built with the test tree but never add_test()'d, so the unit harness
// never runs it on its own.
//
// It exercises the one fork() shape test/row_id.h must survive: a child that
// reports a row BEFORE its parent has reported any. The parent's lazily opened
// FILE* does not exist yet when it forks, so the two processes open the ID file
// independently — and if either open truncated it, the other's rows would be
// lost and the harness would see fewer IDs than the 3 rows this prints.
// waitpid() makes the order deterministic: the child's row is written first.
#include "row_id.h"

#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    const pid_t pid = fork();
    if (pid < 0) return 2;
    if (pid == 0) {
        report_row_id("FORK-CHILD-01");
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 3;
    report_row_id("FORK-PARENT-01");
    report_row_id("FORK-PARENT-02");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", 3, 3, 0, 0);
    return 0;
}
