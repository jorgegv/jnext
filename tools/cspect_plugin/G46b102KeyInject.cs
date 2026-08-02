// CSpect plugin: G46(b) #102 (TX-1696 freeze) — synthetic keyboard injection
// + per-frame register/framebuffer-hash trace, so CSpect can be driven
// through the exact same NextZXOS Browser navigation + in-game key schedule
// as jnext's tools/g46b_102_repro*.sh scripts, WITHOUT a working on-screen
// window (this sandbox's Xvfb has no GL, so CSpect's SDL/OpenTK window never
// maps and xdotool has nothing to send events to).
//
// Mechanism: CSpect has no "inject keypress" plugin API, but it DOES let a
// plugin register as the Port_Read handler for arbitrary ports (see
// i2C_Sample/i2C_NULL_Device.cs upstream). We register every port whose low
// byte is $FE (the ULA/keyboard decode) across all 256 high-byte row
// selectors, and on each read fetch CSpect's own real value via a
// reentrancy-guarded InPort() call, then clear the column bits for whatever
// virtual keys our own frame-driven schedule says are "held" right now —
// exactly mirroring src/input/keyboard.cpp's row/col matrix and its
// DOWN=CapsShift+'6' compound fold (membrane.vhd:236-238 semantics).
//
// Tick() fires once per emulated video frame (per Plugin.iPlugin doc) — we
// use it both as our own frame counter (driving the key schedule, mirroring
// jnext's --delayed-keypress-frames semantics: press for 5 frames) and as
// the sample point for a per-frame CSV trace (PC/regs + an FNV-1a hash of
// CSpect's own composited last-frame buffer via GetGlobal(eGlobal.last_frame)
// — the CSpect analogue of jnext's forced render_frame()+framebuffer hash in
// JNEXT_G46B_PCTRACE). A run where the hash (and regs) go static forever is
// CSpect's own freeze signature, directly comparable to jnext's.
//
// Configuration env vars:
//   G46B102_KEYINJECT_TRACE     — output CSV path (default /tmp/g46b-102-cspect/trace.csv)
//   G46B102_KEYINJECT_SPAMEND   — last frame of the space-mash blanket (default 4200)
//   G46B102_KEYINJECT_SPAMSTRIDE— blanket stride in frames (default 100)
//
// Build (CSPECT_DIR = your CSpect installation directory):
//   cd tools/cspect_plugin
//   mcs -target:library \
//     -r:"$CSPECT_DIR/Plugin.dll" \
//     -out:G46b102KeyInject.dll G46b102KeyInject.cs
//
// Install (CSpect scans its own exe dir for plugin DLLs; NOT part of the
// jnext repo — copy into place before each CSpect launch, same convention as
// CSpectFullTrace.dll):
//   cp G46b102KeyInject.dll "$CSPECT_DIR"/
//
// G46(b) #102 session 2, 2026-07-26.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Plugin;

namespace JnextG46b
{
    public class G46b102KeyInject : iPlugin
    {
        private iCSpect cspect;
        private StreamWriter sw;
        private bool m_Internal = false;

        private long frame = 0;
        private long spam_end = 4200;
        private long spam_stride = 100;
        private string out_path = "/tmp/g46b-102-cspect/trace.csv";
        private string dump_dir = "/tmp/g46b-102-cspect";
        private HashSet<long> dump_frames = new HashSet<long>();

        // ---- Matrix position + schedule -----------------------------------
        private struct Pos { public int row, col; public Pos(int r, int c) { row = r; col = c; } }
        private struct Press { public long start; public Pos a; public Pos b; public bool has_b; }

        private static Pos SPACE = new Pos(7, 0);
        private static Pos ENTER = new Pos(6, 0);
        private static Pos CAPS  = new Pos(0, 0);
        private static Pos KEY6  = new Pos(4, 4);   // DOWN folds to CS + '6'

        private List<Press> schedule = new List<Press>();
        // Currently-held presses: (expiry frame, positions). Const HOLD_FRAMES
        // mirrors jnext's headless_app.cpp AutoKey duration ("press for 5 frames").
        private const int HOLD_FRAMES = 5;
        private struct Active { public long expiry; public Pos a; public Pos b; public bool has_b; }
        private List<Active> active = new List<Active>();

        // rowClear[r] = 5-bit mask (bit=1 => this column currently held/pressed)
        private int[] rowClear = new int[8];

        private void AddPress(long f, Pos a) {
            Press p = new Press(); p.start = f; p.a = a; p.has_b = false;
            schedule.Add(p);
        }
        private void AddCompound(long f, Pos a, Pos b) {
            Press p = new Press(); p.start = f; p.a = a; p.b = b; p.has_b = true;
            schedule.Add(p);
        }

        // Build the exact nav + spam schedule from tools/g46b_102_repro.sh /
        // g46b_102_repro_stride.sh (kp() calls), so CSpect sees the same
        // relative key timeline as jnext.
        private void BuildSchedule() {
            AddPress(450, SPACE);
            AddPress(700, ENTER);

            long f = 750;
            for (int i = 0; i < 6; i++) { AddCompound(f, CAPS, KEY6); f += 20; }
            AddPress(f, ENTER); f += 20;

            for (int i = 0; i < 4; i++) { AddCompound(f, CAPS, KEY6); f += 20; }
            AddPress(f, ENTER); f += 20;

            for (int i = 0; i < 15; i++) { AddCompound(f, CAPS, KEY6); f += 20; }
            AddPress(f, ENTER); f += 20;

            for (int i = 0; i < 5; i++) { AddCompound(f, CAPS, KEY6); f += 20; }
            AddPress(f, ENTER); f += 20;

            for (long sf = 2000; sf <= spam_end; sf += spam_stride)
                AddPress(sf, SPACE);

            schedule.Sort((x, y) => x.start.CompareTo(y.start));
        }

        public List<sIO> Init(iCSpect _cspect)
        {
            cspect = _cspect;

            string envTrace = Environment.GetEnvironmentVariable("G46B102_KEYINJECT_TRACE");
            if (!string.IsNullOrEmpty(envTrace)) out_path = envTrace;

            string envEnd = Environment.GetEnvironmentVariable("G46B102_KEYINJECT_SPAMEND");
            if (!string.IsNullOrEmpty(envEnd)) { long v; if (long.TryParse(envEnd, out v)) spam_end = v; }

            string envStride = Environment.GetEnvironmentVariable("G46B102_KEYINJECT_SPAMSTRIDE");
            if (!string.IsNullOrEmpty(envStride)) { long v; if (long.TryParse(envStride, out v)) spam_stride = v; }

            // Comma-separated list of frame numbers at which to dump the raw
            // ULA screen (0x4000-0x5AFF, 6912 bytes) to
            // {dump_dir}/screen_{frame}.bin — avoids DZRP polling entirely
            // (observed to stall CSpect's free-run when connected/
            // disconnected repeatedly mid-run).
            string envDump = Environment.GetEnvironmentVariable("G46B102_KEYINJECT_DUMPFRAMES");
            if (!string.IsNullOrEmpty(envDump)) {
                foreach (string s in envDump.Split(',')) {
                    long v;
                    if (long.TryParse(s.Trim(), out v)) dump_frames.Add(v);
                }
            }
            string envDumpDir = Environment.GetEnvironmentVariable("G46B102_KEYINJECT_DUMPDIR");
            if (!string.IsNullOrEmpty(envDumpDir)) dump_dir = envDumpDir;
            if (dump_frames.Count > 0) Directory.CreateDirectory(dump_dir);

            BuildSchedule();

            try {
                string dir = Path.GetDirectoryName(out_path);
                if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
                sw = new StreamWriter(out_path, append: false, encoding: Encoding.ASCII, bufferSize: 1 << 16);
                sw.AutoFlush = false;
                sw.WriteLine("frame,pc,af,bc,de,hl,sp,ix,iy,fb_hash");
            } catch (Exception ex) {
                Console.WriteLine("G46b102KeyInject: cannot open output: " + ex.Message);
                sw = null;
            }

            Console.WriteLine(string.Format(
                "G46b102KeyInject: {0} scheduled presses, spam_end={1} stride={2}, trace -> {3}",
                schedule.Count, spam_end, spam_stride, out_path));

            // Register Port_Read on every port with low byte $FE (all 256
            // high-byte row selectors) — the ULA/keyboard decode.
            var hooks = new List<sIO>(256);
            for (int hi = 0; hi <= 0xFF; hi++)
                hooks.Add(new sIO((hi << 8) | 0xFE, eAccess.Port_Read, 0, 0));
            return hooks;
        }

        public void Quit()
        {
            if (sw == null) return;
            try { sw.Flush(); sw.Close(); sw = null; } catch { }
        }

        public void Reset() { }
        public void OSTick() { }
        public bool KeyPressed(int _id) { return false; }
        public bool Write(eAccess _type, int _port, int _id, byte _value) { return false; }

        public void Tick()
        {
            // 1. Activate any schedule entries starting this frame.
            foreach (Press p in schedule) {
                if (p.start == frame) {
                    Active a = new Active();
                    a.expiry = frame + HOLD_FRAMES;
                    a.a = p.a; a.b = p.b; a.has_b = p.has_b;
                    active.Add(a);
                    Console.WriteLine(string.Format(
                        "G46b102KeyInject: PRESS frame={0} pos=({1},{2}){3}",
                        frame, a.a.row, a.a.col,
                        a.has_b ? string.Format("+({0},{1})", a.b.row, a.b.col) : ""));
                }
            }
            // 2. Expire held presses.
            active.RemoveAll(a => a.expiry <= frame);

            // 3. Recompute per-row clear mask from currently active presses.
            for (int r = 0; r < 8; r++) rowClear[r] = 0;
            foreach (Active a in active) {
                rowClear[a.a.row] |= (1 << a.a.col);
                if (a.has_b) rowClear[a.b.row] |= (1 << a.b.col);
            }

            // 4. Per-frame trace row. This Plugin.dll build predates
            // eGlobal.last_frame (composited framebuffer), so the "picture
            // changed" signal is a hash of the ULA screen (0x4000-0x5AFF,
            // CPU-view, follows current bank mapping) plus all 128 hardware
            // sprite records (x/y/pattern/attr) — sufficient to detect
            // on-screen motion for a game that is known to use sprites for
            // the ship/bullet/coins (per the session-1 characterisation).
            if (sw != null) {
                Z80Regs r = cspect.GetRegs();
                ulong hash = 1469598103934665603UL;  // FNV-1a 64-bit offset basis
                try {
                    byte[] screen = cspect.Peek(0x4000, 6912, null);
                    for (int i = 0; i < screen.Length; i++) {
                        hash ^= screen[i];
                        hash *= 1099511628211UL;  // FNV-1a 64-bit prime
                    }
                    for (int i = 0; i < 128; i++) {
                        SSprite s = cspect.GetSprite(i);
                        hash ^= s.x; hash *= 1099511628211UL;
                        hash ^= s.y; hash *= 1099511628211UL;
                        hash ^= s.paloff_mirror_flip_rotate_xmsb; hash *= 1099511628211UL;
                        hash ^= s.visible_name; hash *= 1099511628211UL;
                        hash ^= s.H_N6_0_XX_YY_Y8; hash *= 1099511628211UL;
                    }
                } catch { }

                sw.WriteLine(string.Format(
                    "{0},{1:X4},{2:X4},{3:X4},{4:X4},{5:X4},{6:X4},{7:X4},{8:X4},{9:X16}",
                    frame, r.PC & 0xFFFF, r.AF & 0xFFFF, r.BC & 0xFFFF, r.DE & 0xFFFF,
                    r.HL & 0xFFFF, r.SP & 0xFFFF, r.IX & 0xFFFF, r.IY & 0xFFFF, hash));
                if ((frame % 100) == 0) {
                    sw.Flush();
                    Console.WriteLine(string.Format("G46b102KeyInject: frame {0} pc=${1:X4}", frame, r.PC & 0xFFFF));
                }
            }

            // 5. Optional raw-screen dump for offline PNG rendering.
            if (dump_frames.Contains(frame)) {
                try {
                    byte[] screen = cspect.Peek(0x4000, 6912, null);
                    string path = Path.Combine(dump_dir, string.Format("screen_{0}.bin", frame));
                    File.WriteAllBytes(path, screen);
                    Console.WriteLine("G46b102KeyInject: dumped " + path);
                } catch (Exception ex) {
                    Console.WriteLine("G46b102KeyInject: dump failed: " + ex.Message);
                }
            }

            frame++;
        }

        public byte Read(eAccess _type, int _port, int _id, out bool _isvalid)
        {
            _isvalid = false;
            if (_type != eAccess.Port_Read) return 0;
            if ((_port & 0xFF) != 0xFE) return 0;

            if (m_Internal) return 0xFF;   // reentrant InPort() call below — no-op
            m_Internal = true;
            int real = cspect.InPort((ushort)_port);
            m_Internal = false;

            int hi = (_port >> 8) & 0xFF;
            int result = real;
            for (int row = 0; row < 8; row++) {
                if (((hi >> row) & 1) == 0 && rowClear[row] != 0) {
                    result &= ~rowClear[row];
                }
            }

            _isvalid = true;
            return (byte)(result & 0xFF);
        }
    }
}
