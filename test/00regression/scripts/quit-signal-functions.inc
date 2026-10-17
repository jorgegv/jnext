# Shared by quit-signal-func.sh and quit-signal-rzx-func.sh (GH #364). Sourced.
# Needs qs_dir, qs_sdl, qs_faults, qs_rc from the caller.

# qs_case <frontend> <tag> <signal-name> <extra jnext args...>
# Runs jnext under scripts/quit-signal-run.py (a python parent) and signals
# jnext's own PID. qs_rc is the Popen return code: -15 / -2 = killed BY that
# signal (WIFSIGNALED), which bash's 143/130 cannot tell from exit(143).
# Appends to qs_faults on any deviation.
# A tag ending in -paused attaches a DZRP client and pauses the machine first
# (headless only); the WAV is then not asserted (no frame ran while paused).
qs_case() {
    local fe=$1 tag=$2 sig=$3 i jpid="" ppid wav="$qs_dir/$2.wav" log="$qs_dir/$2.log"
    local pidf="$qs_dir/$2.pid" rcf="$qs_dir/$2.rc" readyf="$qs_dir/$2.ready"
    local want_rc port=0 paused=0 launcher
    shift 3
    launcher="$(dirname "${BASH_SOURCE[0]}")/quit-signal-run.py"
    case $sig in TERM) want_rc=-15 ;; INT) want_rc=-2 ;; esac
    local extra=()
    if [[ $tag == *-paused ]]; then
        paused=1
        port=$(LANG=C python3 -I -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
        extra=(--dzrp-port "$port")
    fi
    local lead=(python3 -I "$launcher" "$pidf" "$rcf" "$readyf" "$port" "$PROJECT_DIR/tools/cspect_dzrp" --)
    case $fe in
        headless)
            "${lead[@]}" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
                --machine 48k --wav-record "$wav" ${extra[@]+"${extra[@]}"} "$@" >"$log" 2>&1 & ppid=$! ;;
        qt)
            env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            "${lead[@]}" "$JNEXT" "${SD_CARD_ARGS[@]}" \
                --machine 48k --wav-record "$wav" "$@" >"$log" 2>&1 & ppid=$! ;;
        sdl)
            env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
            "${lead[@]}" "$qs_sdl" "${SD_CARD_ARGS[@]}" \
                --machine 48k --wav-record "$wav" "$@" >"$log" 2>&1 & ppid=$! ;;
    esac

    # jnext's PID (bounded, 10 s).
    for ((i = 0; i < 100; i++)); do
        [[ -s "$pidf" ]] && { jpid=$(<"$pidf"); break; }
        sleep 0.1
    done
    # Ready (bounded, 30 s): samples are being written, or the client paused it.
    local ready=0
    if [[ -n $jpid ]]; then
        for ((i = 0; i < 150; i++)); do
            if (( paused )); then
                [[ -e "$readyf" ]] && { ready=1; break; }
            elif [[ -f "$wav" && $(stat -c %s "$wav" 2>/dev/null || stat -f %z "$wav") -gt 44 ]]; then
                ready=1; break
            fi
            [[ -e "$rcf" ]] && break
            sleep 0.2
        done
    fi
    if [[ $ready != 1 ]]; then
        [[ -z $jpid ]] || kill -KILL "$jpid" 2>/dev/null || true
        kill -KILL "$ppid" 2>/dev/null || true
        wait "$ppid" 2>/dev/null || true
        qs_faults+=("$tag: never became ready ($( ((paused)) && echo paused by a client || echo producing samples))")
        return 0
    fi

    kill -"$sig" "$jpid"

    # Gone within 10 s: the launcher writes the return code when jnext ends.
    local gone=0
    for ((i = 0; i < 100; i++)); do
        [[ -s "$rcf" ]] && { gone=1; break; }
        sleep 0.1
    done
    if [[ $gone != 1 ]]; then
        kill -KILL "$jpid" 2>/dev/null || true
        wait "$ppid" 2>/dev/null || true
        qs_faults+=("$tag: still running 10 s after SIG$sig (killed)")
        return 0
    fi
    wait "$ppid" 2>/dev/null || true
    qs_rc=$(<"$rcf")
    [[ "$qs_rc" == "$want_rc" ]] || qs_faults+=("$tag: returncode $qs_rc (want $want_rc: terminated BY SIG$sig)")

    local n
    n=$(grep -cF "SIG$sig received: quitting" "$log" || true)
    [[ "$n" == 1 ]] || qs_faults+=("$tag: log has 'SIG$sig received: quitting' $n times (want 1)")

    # WAV: header consistent with the file, and samples behind it.
    if (( ! paused )); then
        if ! python3 -I - "$wav" <<'PY'
import os, sys, wave
p = sys.argv[1]
w = wave.open(p, "rb")
n = w.getnframes() * w.getsampwidth() * w.getnchannels()
sys.exit(0 if n > 0 and n == os.path.getsize(p) - 44 else 1)
PY
        then
            qs_faults+=("$tag: WAV header not consistent with the file (recording not finalized)")
        fi
    fi
    return 0
}
