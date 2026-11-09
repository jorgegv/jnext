# Sourced by bench.sh and web/browser-bench.sh: ONE benchmark at a time on this host.
#
# The lock lives under the user's cache directory, not in the checkout: every git worktree has its own
# test/bench, so a lock beside the scripts never serialised two worktrees (two agents' benches overlapped
# on 2026-11-09 and contaminated each other's numbers). JNEXT_BENCH_LOCK overrides the path (the self-test).
# A real blocking flock, not a process scan; the descriptor stays open for the life of the calling shell.
bench_take_lock() {
    local lock=${JNEXT_BENCH_LOCK:-$HOME/.cache/jnext/bench.lock}
    mkdir -p "$(dirname "$lock")" || return 1
    exec 200>"$lock" || return 1
    if ! flock -n 200; then
        echo "bench: another benchmark holds $lock - waiting for it to finish..."
        flock 200
    fi
}

# Start a background child WITHOUT the lock descriptor. The lock is held for exactly as long as the calling shell
# lives: a child that inherited fd 200 and outlives it (a detached browser, an Xvfb) would hold the lock for every
# later bench on the host. Sets BENCH_CHILD_PID.
bench_spawn() {
    ( exec 200>&-; exec "$@" ) &
    BENCH_CHILD_PID=$!
}
