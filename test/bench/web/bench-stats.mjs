// Pure functions of the browser bench (make bench-web): per-cell metrics, the verdict, the
// INVALID reasons, the repeat rule, completeness, the WEBBENCH line and the markdown table.
// No I/O, no clock, no browser: the rows WBENCH-01..09 (bench-stats-test.mjs) run on synthetic data.
//
// A raw cell is what the page measured over the window (bench.js):
//   costs[]   each requestAnimationFrame callback's cost in ms
//   deltas[]  the emulator frames that callback ran (frame counter after minus before)
//   wall_ms   the window's wall time        period_ms / period_end_ms  the frame period at both ends
//   cpu_mhz / cpu_mhz_end    the effective CPU speed at both ends
//   audio[]   each ScriptProcessor callback's cost in ms        calib_ms  the calibration kernel's median
//   gl (bool) audio_state ('running' ...) capped (the 60 s cap ended the window) error (string or null)
//
// Full speed is the program's OWN refresh, 1000 / frame period (49.36 Hz for 128K timing, 58.15 Hz
// for 60 Hz programs), never a literal 50 fps. PASS: both the emulated and the SHOWN rate reach
// 0.99 x refresh. Shown = callbacks that ran at least one frame: the browser composites once per
// rAF, so a callback that caught up two frames shows one picture.

export const WINDOW_FRAMES = 600;
export const PASS_RATIO = 0.99;
export const THROTTLE_BAND = [1.8, 2.2];     // x2 calibration / unthrottled calibration
export const BUSY_FACTOR = 1.10;             // a cell's calibration vs the pass reference (its median)
export const REPEAT_EMU_RATIO = 0.95;
export const REPEAT_HEADROOM = 1.10;

const sum = (a) => a.reduce((x, y) => x + y, 0);

// Nearest rank: the smallest value with at least p percent of the data at or below it.
export function percentile(values, p) {
  if (values.length === 0) return 0;
  const s = values.slice().sort((a, b) => a - b);
  const rank = Math.ceil((p / 100) * s.length);
  return s[Math.min(s.length, Math.max(1, rank)) - 1];
}

export function metrics(raw) {
  const frames = sum(raw.deltas);
  const wall_s = raw.wall_ms / 1000;
  const refresh = 1000 / raw.period_end_ms;
  const shown = raw.deltas.filter((d) => d >= 1).length;
  const p95 = percentile(raw.costs, 95);
  return {
    frames, wall_s, refresh,
    emu_fps: frames / wall_s,
    shown_fps: shown / wall_s,
    ms_per_frame: sum(raw.costs) / frames,
    audio_ms_per_frame: sum(raw.audio || []) / frames,
    p50: percentile(raw.costs, 50), p95, max: percentile(raw.costs, 100),
    headroom: raw.period_end_ms / p95,
  };
}

// The rate test alone, on computed metrics (so a row can sit exactly on the threshold).
export function rateVerdict(m) {
  const need = PASS_RATIO * m.refresh;
  return m.emu_fps >= need && m.shown_fps >= need ? 'PASS' : 'FAIL';
}

// ctx: { base_calib_ms: the unthrottled pass's median calibration (x2 cells only),
//        pass_ref_calib_ms: the pass's median calibration, x2: bool, window_frames }
// Returns { verdict: PASS|FAIL|INVALID, reason, m }. An INVALID cell has m null, except
// host_got_busier, which keeps its metrics and what the rate test would have said (`would`): the
// run is not counted, but its numbers are still shown.
export function judge(raw, ctx = {}) {
  const want = ctx.window_frames || WINDOW_FRAMES;
  const bad = (reason) => ({ verdict: 'INVALID', reason, m: null });
  if (raw.error) return bad('load_error');
  if (!raw.gl) return bad('no_webgl');
  if (raw.period_ms !== raw.period_end_ms) return bad('frame_period_changed');
  const frames = sum(raw.deltas);
  if (raw.capped && frames < want) return bad('cap_before_window');
  if (raw.audio_state !== 'running') return bad('audio_not_running');
  if (ctx.x2) {
    const r = raw.calib_ms / ctx.base_calib_ms;
    if (!(r >= THROTTLE_BAND[0] && r <= THROTTLE_BAND[1])) return bad('throttle_not_in_effect');
  }
  const m = metrics(raw);
  if (ctx.pass_ref_calib_ms && raw.calib_ms > BUSY_FACTOR * ctx.pass_ref_calib_ms) {
    return { verdict: 'INVALID', reason: 'host_got_busier', m, would: rateVerdict(m) };
  }
  return { verdict: rateVerdict(m), reason: '-', m };
}

// Cells (one run each, with verdict and metrics) that need two more runs.
export function needsRepeat(c) {
  if (c.verdict === 'FAIL') return c.m.emu_fps >= REPEAT_EMU_RATIO * c.m.refresh;
  if (c.verdict === 'PASS') return c.m.headroom < REPEAT_HEADROOM;
  return false;
}

// Runs of one cell -> the counted result: majority verdict among the valid runs (a tie is FAIL), the
// representative run being the valid one with the median emu_fps.
export function settle(runs) {
  const valid = runs.filter((r) => r.verdict !== 'INVALID');
  if (valid.length === 0) return { ...runs[0], runs: runs.length };
  const pass = valid.filter((r) => r.verdict === 'PASS').length;
  const verdict = pass * 2 > valid.length ? 'PASS' : 'FAIL';
  const byFps = valid.slice().sort((a, b) => a.m.emu_fps - b.m.emu_fps);
  const rep = byFps[Math.floor((byFps.length - 1) / 2)];
  return { ...rep, verdict, runs: runs.length };
}

// results: [{config, program, ...}]; every program x config of the declared set must be there.
export function checkComplete(results, programs, configs) {
  const have = new Set(results.map((r) => r.config + '/' + r.program));
  const missing = [];
  for (const c of configs) for (const p of programs) if (!have.has(c + '/' + p)) missing.push(c + '/' + p);
  if (missing.length) throw new Error('missing cells: ' + missing.join(' '));
}

const f = (v, d) => (Number.isFinite(v) ? v.toFixed(d) : 'nan');

// One physical line, key=value, greppable like BENCH.
export function formatLine(c) {
  const m = c.m;
  const parts = [`config=${c.config}`, `program=${c.program}`, `verdict=${c.verdict}`, `reason=${c.reason || '-'}`];
  if (c.would) parts.push(`would=${c.would}`);
  if (m) {
    parts.push(`frames=${m.frames}`, `wall_s=${f(m.wall_s, 3)}`, `refresh=${f(m.refresh, 3)}`,
               `emu_fps=${f(m.emu_fps, 2)}`, `shown_fps=${f(m.shown_fps, 2)}`,
               `ms_per_frame=${f(m.ms_per_frame, 3)}`, `audio_ms_per_frame=${f(m.audio_ms_per_frame, 3)}`,
               `p50=${f(m.p50, 3)}`, `p95=${f(m.p95, 3)}`, `max=${f(m.max, 3)}`, `headroom=${f(m.headroom, 2)}`);
  }
  parts.push(`speed=${c.speed || '-'}`, `calib_ms=${f(c.calib_ms, 2)}`, `runs=${c.runs || 1}`);
  return 'WEBBENCH ' + parts.join(' ');
}

export function parseLine(line) {
  if (!line.startsWith('WEBBENCH ')) throw new Error('not a WEBBENCH line');
  const o = {};
  for (const kv of line.slice(9).split(' ')) { const i = kv.indexOf('='); o[kv.slice(0, i)] = kv.slice(i + 1); }
  return o;
}

export function speedLabel(raw) {
  const a = raw.cpu_mhz, b = raw.cpu_mhz_end;
  return a === b ? String(a) : `${a}>${b}`;
}

// The markdown table: one row per program, one column per config.
export function markdownTable(results, programs, configs) {
  checkComplete(results, programs, configs);
  const by = new Map(results.map((r) => [r.config + '/' + r.program, r]));
  const cell = (r) => (r.verdict === 'INVALID' ? `INVALID (${r.reason}${r.would ? '; would ' + r.would : ''})${r.m ? ' ' + f(r.m.ms_per_frame, 2) + ' ms/f ' + f(r.m.emu_fps, 1) + '/' + f(r.m.refresh, 1) : ''}`
    : `${r.verdict} ${f(r.m.ms_per_frame, 2)} ms/f ${f(r.m.emu_fps, 1)}/${f(r.m.refresh, 1)} p95 ${f(r.m.p95, 1)} ${r.speed ? 'x' + r.speed : ''}`.trim());
  const out = ['| program | ' + configs.join(' | ') + ' |', '|---|' + configs.map(() => '---|').join('')];
  for (const p of programs) out.push(`| ${p} | ` + configs.map((c) => cell(by.get(c + '/' + p))).join(' | ') + ' |');
  return out.join('\n');
}
