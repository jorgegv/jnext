# Shared by quit-signal-func.sh and quit-signal-rzx-func.sh (GH #364). Sourced.
# Needs qs_dir, qs_sdl, qs_faults, qs_rc from the caller.

    # qs_case <frontend> <tag> <signal-name> <extra jnext args...>
    # Sets qs_rc to the child's status; appends to qs_faults on any deviation.
    qs_case() {
        local fe=$1 tag=$2 sig=$3 i pid="" wav="$qs_dir/$2.wav" log="$qs_dir/$2.log"
        local want_rc
        shift 3
        case $sig in TERM) want_rc=143 ;; INT) want_rc=130 ;; esac
        local exec_py='import os,signal,sys; signal.signal(signal.SIGINT, signal.SIG_DFL); os.execvp(sys.argv[1], sys.argv[1:])'
        case $fe in
            headless)
                python3 -I -c "$exec_py" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
                    --machine 48k --wav-record "$wav" "$@" >"$log" 2>&1 & pid=$! ;;
            qt)
                env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
                python3 -I -c "$exec_py" "$JNEXT" "${SD_CARD_ARGS[@]}" \
                    --machine 48k --wav-record "$wav" "$@" >"$log" 2>&1 & pid=$! ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                python3 -I -c "$exec_py" "$qs_sdl" "${SD_CARD_ARGS[@]}" \
                    --machine 48k --wav-record "$wav" "$@" >"$log" 2>&1 & pid=$! ;;
        esac

        # Ready: samples are being written (bounded, 30 s).
        local ready=0
        for ((i = 0; i < 150; i++)); do
            if [[ -f "$wav" && $(stat -c %s "$wav" 2>/dev/null || stat -f %z "$wav") -gt 44 ]]; then
                ready=1; break
            fi
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.2
        done
        if [[ $ready != 1 ]]; then
            kill -KILL "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            qs_faults+=("$tag: never started producing samples")
            return 0
        fi

        kill -"$sig" "$pid"

        # Exit within 10 s.
        local gone=0
        for ((i = 0; i < 100; i++)); do
            if ! kill -0 "$pid" 2>/dev/null; then gone=1; break; fi
            sleep 0.1
        done
        if [[ $gone != 1 ]]; then
            kill -KILL "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            qs_faults+=("$tag: still running 10 s after SIG$sig (killed)")
            return 0
        fi
        qs_rc=0
        wait "$pid" || qs_rc=$?
        [[ "$qs_rc" == "$want_rc" ]] || qs_faults+=("$tag: status $qs_rc (want $want_rc)")

        local n
        n=$(grep -cF "SIG$sig received: quitting" "$log" || true)
        [[ "$n" == 1 ]] || qs_faults+=("$tag: log has 'SIG$sig received: quitting' $n times (want 1)")

        # WAV: header consistent with the file, and samples behind it.
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
        return 0
    }

