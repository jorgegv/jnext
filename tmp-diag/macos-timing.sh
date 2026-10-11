#!/bin/bash
# TEMPORARY diagnostic (GH #319 round 2), reverted after the measurement.
set -u
echo "== uname"; uname -a; sysctl -n hw.ncpu; sw_vers
echo "== python time.sleep(ms) mean over 50 (host timer, no jnext)"
python3 - <<'PY'
import time
for ms in (1,2,5,10,16,20):
    n=50; t=time.perf_counter()
    for _ in range(n): time.sleep(ms/1000)
    print("sleep %2d ms -> %.2f ms" % (ms,(time.perf_counter()-t)/n*1000))
PY
SDL=build/sdl-release/jnext
echo "== SD image"; timeout 1200 $SDL --headless --sdcard-download-confirm --delayed-automatic-exit 2 >/dev/null 2>&1; ls -la ~/.jnext/sdcard | head -3
run() {  # run <label> <prefix...> -- args
  local label=$1; shift
  local t0=$(python3 -c 'import time;print(time.perf_counter())')
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$@" >/dev/null 2>&1
  local t1=$(python3 -c 'import time;print(time.perf_counter())')
  python3 -c "print('$label: %.0f ms' % (($t1-$t0)*1000))"
}
for sp in 400 100; do
  run "sdl --speed $sp plain" timeout 60 $SDL --silent --machine 48k --speed $sp --delayed-automatic-exit-frames 100
  run "sdl --speed $sp caffeinate -i" caffeinate -i timeout 60 $SDL --silent --machine 48k --speed $sp --delayed-automatic-exit-frames 100
  run "sdl --speed $sp taskpolicy -B(no)" timeout 60 $SDL --silent --machine 48k --speed $sp --delayed-automatic-exit-frames 100
done
echo "== headless frames 100 (no pacing)"
run "headless 100 frames" timeout 60 $SDL --headless --machine 48k --delayed-automatic-exit-frames 100
echo "== dzrp paused-headless row, 5 attempts: peer output + log timestamps"
for n in 1 2 3 4 5; do
  port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
  log=$(mktemp)
  timeout 60 $SDL --headless --machine 48k --dzrp-port $port --delayed-automatic-exit-frames 100 >"$log" 2>&1 &
  jp=$!
  out=$(timeout 45 python3 test/00regression/dzrp-peer.py paused-headless $port $jp 100 2>&1); rc=$?
  wait $jp; jrc=$?
  echo "-- attempt $n peer_rc=$rc jnext_rc=$jrc: $(echo "$out" | tail -1)"
  grep -E "listening|CMD_INIT|automatic exit|Headless mode shutdown|jnext 1" "$log" | cut -c1-160
done
