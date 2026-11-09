// Rows WBENCH-01..09 (make wasm-browser-test, run by test/browser/browser-smoke.sh on the SDK's
// node): the arithmetic of the browser bench on synthetic data. No browser, no clock, no speed:
// a wrong verdict would make a wrong table, so it is what these pin. Expected values are derived
// by hand from the definitions in bench-stats.mjs's header (nearest-rank percentile; full speed =
// the program's own refresh 1000 / frame period; shown = callbacks that ran a frame).
import { percentile, metrics, rateVerdict, judge, checkComplete, formatLine, parseLine, paceMetrics, formatPaceLine } from './bench-stats.mjs';

const rows = [];
const row = (id, text, fn) => rows.push({ id, text, fn });
const must = (cond, detail) => { if (!cond) throw new Error(detail || 'condition failed'); };
const near = (a, b) => Math.abs(a - b) < 1e-9;

const GOOD = () => ({
  costs: new Array(600).fill(5), deltas: new Array(600).fill(1), wall_ms: 12000,
  period_ms: 20.2594, period_end_ms: 20.2594, cpu_mhz: 28, cpu_mhz_end: 28, audio: new Array(300).fill(0.1),
  calib_ms: 50, gl: true, audio_state: 'running', capped: false, error: null,
});
// 300 callbacks, 200 x 1 frame + 100 x 2 frames = 400 frames, total cost 4000 ms, wall 8 s
const MIX = () => ({ ...GOOD(), costs: new Array(300).fill(4000 / 300), deltas: [...new Array(200).fill(1), ...new Array(100).fill(2)], wall_ms: 8000 });

row('WBENCH-01', 'costs 1..100 ms: p50 50, p95 95, max 100; 10 values: p95 is the 10th (nearest rank, a fractional rank rounds up)', () => {
  const c = Array.from({ length: 100 }, (_, i) => i + 1).reverse();
  must(percentile(c, 50) === 50 && percentile(c, 95) === 95 && percentile(c, 100) === 100,
       `${percentile(c, 50)} ${percentile(c, 95)} ${percentile(c, 100)}`);
  // a fractional rank rounds UP: 10 values, p95 -> rank 9.5 -> the 10th
  const t = [3, 1, 2, 10, 9, 8, 7, 6, 5, 4];
  must(percentile(t, 95) === 10 && percentile(t, 50) === 5 && percentile(t, 10) === 1,
       `${percentile(t, 95)} ${percentile(t, 50)} ${percentile(t, 10)}`);
});

row('WBENCH-02', '200 x 1 + 100 x 2 frames, cost 4000 ms, wall 8 s: 400 frames, 50.0 emu fps, 10.0 ms per frame', () => {
  const m = metrics(MIX());
  must(m.frames === 400 && near(m.emu_fps, 50) && near(m.ms_per_frame, 10), JSON.stringify(m));
});

row('WBENCH-03', 'the same window: shown fps 300 / 8 = 37.5 (a 2-frame callback counts once)', () => {
  const m = metrics(MIX());
  must(near(m.shown_fps, 37.5), String(m.shown_fps));
});

row('WBENCH-04', 'emu = shown = 0.99 x refresh exactly passes; minus 0.001 fails', () => {
  const refresh = 1000 / 20.2594, at = 0.99 * refresh;
  const a = rateVerdict({ refresh, emu_fps: at, shown_fps: at });
  const b = rateVerdict({ refresh, emu_fps: at - 0.001, shown_fps: at - 0.001 });
  must(a === 'PASS' && b === 'FAIL', `${a} ${b}`);
});

row('WBENCH-05', 'period 17.198 ms (58.15 Hz), emu = shown = 50.0: FAIL (the threshold follows the refresh, not 50)', () => {
  const raw = { ...GOOD(), period_ms: 17.198, period_end_ms: 17.198 };   // 600 frames in 12 s = 50.0 fps
  const j = judge(raw);
  must(j.verdict === 'FAIL' && near(j.m.emu_fps, 50) && near(j.m.shown_fps, 50), JSON.stringify(j));
});

row('WBENCH-06', 'emu at refresh, shown 30: FAIL', () => {
  const raw = { ...GOOD(), period_ms: 20, period_end_ms: 20, costs: new Array(360).fill(5),
                deltas: [...new Array(120).fill(1), ...new Array(240).fill(2)] };   // 600 frames, 360 callbacks, 12 s
  const j = judge(raw);
  must(j.verdict === 'FAIL' && near(j.m.emu_fps, 50) && near(j.m.shown_fps, 30), JSON.stringify(j));
});

row('WBENCH-07', 'period changed, AudioContext suspended, x2 ratio 1.5, 60 s cap with 400 frames, calibration 1.1x over the reference: each INVALID with its own reason', () => {
  const cases = [
    [{ ...GOOD(), period_end_ms: 17.198 }, {}],
    [{ ...GOOD(), audio_state: 'suspended' }, {}],
    [{ ...GOOD(), calib_ms: 75 }, { x2: true, base_calib_ms: 50 }],
    [{ ...GOOD(), deltas: new Array(400).fill(1), costs: new Array(400).fill(5), capped: true, wall_ms: 60000 }, {}],
  ];
  const got = cases.map(([raw, ctx]) => judge(raw, ctx));
  const reasons = got.map((g) => g.reason);
  must(got.every((g) => g.verdict === 'INVALID' && g.m === null), JSON.stringify(got.map((g) => g.verdict)));
  must(new Set(reasons).size === 4 && reasons.every((r) => /^[a-z_]+$/.test(r) && r !== '-'), JSON.stringify(reasons));
  must(judge(GOOD(), { x2: true, base_calib_ms: 25 }).verdict === 'PASS', 'ratio 2.0 must be in band');
  // a cell whose calibration is above 1.10 x the pass reference is INVALID but keeps its numbers
  const busy = judge({ ...GOOD(), calib_ms: 55.1 }, { pass_ref_calib_ms: 50 });
  const edge = judge({ ...GOOD(), calib_ms: 55 }, { pass_ref_calib_ms: 50 });
  must(busy.verdict === 'INVALID' && busy.reason === 'host_got_busier' && busy.would === 'PASS' && busy.m && near(busy.m.emu_fps, 50),
       JSON.stringify(busy));
  must(edge.verdict === 'PASS', 'exactly 1.10 x the reference is not busier: ' + JSON.stringify(edge));
});

row('WBENCH-08', 'a results set lacking firefox/beast: the summary throws, naming it', () => {
  const progs = ['beast', 'red'], cfgs = ['chrome', 'firefox'];
  const all = cfgs.flatMap((config) => progs.map((program) => ({ config, program })));
  checkComplete(all, progs, cfgs);
  let msg = null;
  try { checkComplete(all.filter((r) => !(r.config === 'firefox' && r.program === 'beast')), progs, cfgs); } catch (e) { msg = e.message; }
  must(msg !== null && msg.includes('firefox/beast') && !msg.includes('chrome/'), String(msg));
});

row('WBENCH-09', 'a fixed cell formats to a pinned WEBBENCH line (field order, number format) that parses back', () => {
  const c = { config: 'chrome', program: 'beast', verdict: 'PASS', reason: '-', speed: '28', calib_ms: 51.234, runs: 1,
              m: { frames: 600, wall_s: 12.5, refresh: 58.1427, emu_fps: 48, shown_fps: 47.5, ms_per_frame: 3.25,
                   audio_ms_per_frame: 0.125, p50: 3, p95: 4.5, max: 9, headroom: 3.822 } };
  const want = 'WEBBENCH config=chrome program=beast verdict=PASS reason=- frames=600 wall_s=12.500 refresh=58.143 ' +
               'emu_fps=48.00 shown_fps=47.50 ms_per_frame=3.250 audio_ms_per_frame=0.125 p50=3.000 p95=4.500 ' +
               'max=9.000 headroom=3.82 speed=28 calib_ms=51.23 runs=1';
  const got = formatLine(c);
  must(got === want, got);
  const p = parseLine(got);
  must(p.config === 'chrome' && p.program === 'beast' && p.emu_fps === '48.00' && p.runs === '1', JSON.stringify(p));
});

row('WBENCH-10', 'pacing counters over a window: 8 s, callbacks {+10,+590,+40,+10}, ticks by frames {+5,+600,+40,+5}, 3 fill events: 6.25 multi-tick callbacks/s, 5.62 two-frame+ ticks/s, 0.62 zero-frame ticks/s; pinned WEBPACE line', () => {
  const raw = { wall_ms: 8000, pace0: [100, 200, 10, 1, 7, 300, 20, 0, 2], pace1: [110, 790, 50, 11, 12, 900, 60, 5, 5] };
  const p = paceMetrics(raw);
  must(near(p.multi_tick_cb_s, 6.25) && near(p.two_frame_ticks_s, 5.625) && near(p.zero_frame_ticks_s, 0.625) && p.fill_events === 3, JSON.stringify(p));
  must(paceMetrics({ wall_ms: 1000 }) === null, 'no counters: null');
  const want = 'WEBPACE config=chrome program=beast multi_tick_cb_s=6.25 two_frame_ticks_s=5.63 zero_frame_ticks_s=0.63 fill_events=3';
  const got = formatPaceLine({ config: 'chrome', program: 'beast' }, p);
  must(got === want, got);
});

let pass = 0, fail = 0;
for (const r of rows) {
  try { r.fn(); pass++; console.log(`PASS ${r.id} ${r.text}`); }
  catch (e) { fail++; console.log(`FAIL ${r.id} ${r.text} [${String(e.message || e).slice(0, 600)}]`); }
}
console.log('\n====================================================');
console.log(`Total: ${String(pass + fail).padStart(4)}  Passed: ${String(pass).padStart(4)}  Failed: ${String(fail).padStart(4)}  Skipped: ${String(0).padStart(4)}`);
process.exit(fail ? 1 : 0);
