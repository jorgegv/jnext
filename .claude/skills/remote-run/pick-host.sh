#!/usr/bin/env bash
# pick-host.sh [--or-first]: print the first remote host (JNEXT_REMOTE_RUN_HOSTS, a space-separated list of ssh
# aliases in preference order; or the single JNEXT_REMOTE_RUN_HOST) where `remote-run.sh --probe` finds a free
# lane right now. Exit 75 and print nothing when none has one; with --or-first print the first host of the list
# instead and exit 0 (for a caller that will wait for a lane there, such as --gate). Exit 2 when no host is
# configured. The caller runs the driver with JNEXT_REMOTE_RUN_HOST=<printed alias>:
#   H=$(pick-host.sh --or-first) && JNEXT_REMOTE_RUN_HOST=$H remote-run.sh --gate <branch> <sha>
# JNEXT_PICK_HOST_DRIVER overrides the driver (test seam).
set -u
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RR=${JNEXT_PICK_HOST_DRIVER:-$HERE/remote-run.sh}
or_first=0
case "${1:-}" in
    --or-first) or_first=1 ;;
    "") ;;
    *) echo "usage: pick-host.sh [--or-first]" >&2; exit 2 ;;
esac
# shellcheck disable=SC2206
hosts=(${JNEXT_REMOTE_RUN_HOSTS:-${JNEXT_REMOTE_RUN_HOST:-}})
[[ ${#hosts[@]} -gt 0 ]] || { echo "pick-host: JNEXT_REMOTE_RUN_HOSTS and JNEXT_REMOTE_RUN_HOST are both unset" >&2; exit 2; }
for h in "${hosts[@]}"; do
    if env JNEXT_REMOTE_RUN_HOST="$h" "$RR" --probe >&2; then echo "$h"; exit 0; fi
done
if [[ $or_first -eq 1 ]]; then echo "${hosts[0]}"; exit 0; fi
exit 75
