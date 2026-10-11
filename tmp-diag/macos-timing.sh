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
run() {  # run <label> <prefix...> -- args
  local label=$1; shift
  local t0=$(python3 -c 'import time;print(time.perf_counter())')
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy JNEXT_CONFIG_DIR=$(mktemp -d) "$@" >/dev/null 2>&1
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
