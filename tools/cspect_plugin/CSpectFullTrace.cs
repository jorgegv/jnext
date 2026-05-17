// CSpect plugin: full Z80 instruction tracer for G46(b) differential analysis.
//
// Captures the first TRACE_CAPACITY instructions executed by CSpect during
// NextZXOS boot and writes them in the exact same format as jnext's
// TraceLog::export_to_file(), so the two files can be diffed line-by-line
// to find the first divergent instruction.
//
// Output format (identical to jnext trace.cpp export_to_file):
//   {12-digit cycle, zero-padded}  ${4-digit PC hex}  AF={hex4} BC={hex4} DE={hex4} HL={hex4}  AF'={hex4} BC'={hex4} DE'={hex4} HL'={hex4}  IX={hex4} IY={hex4} SP={hex4}  [{SZ5H3PNC}]  {opcode bytes, space-separated}
//
// NOTE: jnext's "cycle" field is T-states. CSpect does NOT expose T-states
// via the plugin API. We substitute the instruction count (monotonically
// increasing by 1 per instruction) as the cycle field. This means the cycle
// column will differ numerically between the two traces, but all other
// columns (PC, regs, flags, opcode bytes) will match if the emulators are
// in sync. The diff script strips the cycle column before comparison.
//
// Hook mechanism: Memory_EXE registered at every address 0x0000..0xFFFF.
// This fires Read() for every M1 cycle (instruction fetch).
//
// Output strategy: write directly to disk and flush every FLUSH_EVERY lines.
// This ensures data survives even if CSpect is killed with SIGKILL.
//
// Configuration env vars:
//   JNEXT_CSPECT_TRACE_FILE  — output path (default /tmp/g46b-cspect-fulltrace/cspect_full_trace.txt)
//   JNEXT_CSPECT_TRACE_CAP   — instruction cap (default 500000)
//
// Build:
//   cd tools/cspect_plugin
//   mcs -target:library \
//     -r:/home/jorgegv/src/spectrum/CSpect3_1_0_0/Plugin.dll \
//     -out:CSpectFullTrace.dll CSpectFullTrace.cs
//
// Install:
//   cp CSpectFullTrace.dll /home/jorgegv/src/spectrum/CSpect3_1_0_0/
//
// EOD-30i+30: 2026-05-16.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Plugin;

namespace JnextG46b
{
    public class CSpectFullTrace : iPlugin
    {
        private iCSpect cspect;
        private StreamWriter sw;

        // Trace state
        private long instr_count = 0;       // monotonic instruction counter (used as "cycle")
        private long trace_cap = 500000;     // stop after this many instructions
        private const int FLUSH_EVERY = 10000; // flush to disk every N instructions

        private string out_path = "/tmp/g46b-cspect-fulltrace/cspect_full_trace.txt";

        // Task 18 sync-point gating: skip the tbblue.fw firmware boot and
        // begin tracing only AFTER the soft reset that hands off to
        // NextZXOS. We detect this by counting Z80 reset events: the FIRST
        // reset is the CSpect cold boot (PC=$0000 with SP=$FFFF at start);
        // the SECOND is the RESET_SOFT issued by tbblue.fw at the end of
        // its boot. Memory_EXE callbacks see the pre-execution state, so
        // we look for the transition `last_pc != 0 → pc == 0` with
        // SP=$FFFF and R≤2 (just-reset registers).
        //
        // Set JNEXT_CSPECT_TRACE_SKIP_RESETS=0 to disable gating (trace
        // from boot start, original G46(b) behaviour). Default = 1 (skip
        // tbblue.fw, trace only from NextZXOS handoff).
        private int reset_count = 0;
        private int skip_resets = 1;  // tracing begins after this many Z80 resets
        private bool tracing_active = false;
        private int last_pc = -1;
        private long total_reads = 0;

        // Determine the byte-length of the Z80/Z80N instruction at the
        // given PC, using cspect.Peek() for memory reads. Mirrors the
        // logic in jnext's src/debug/trace.cpp::z80_instruction_length().
        private int InstrLen(int pc)
        {
            byte op = cspect.Peek((ushort)(pc & 0xFFFF));

            // CB prefix: always 2 bytes
            if (op == 0xCB) return 2;

            // ED prefix
            if (op == 0xED) {
                byte op2 = cspect.Peek((ushort)((pc + 1) & 0xFFFF));
                switch (op2) {
                    // Standard 4-byte: LD (nn),rr / LD rr,(nn)
                    case 0x43: case 0x4B: case 0x53: case 0x5B:
                    case 0x63: case 0x6B: case 0x73: case 0x7B:
                        return 4;
                    // Z80N: NEXTREG nn,nn
                    case 0x91:
                        return 4;
                    // Z80N: NEXTREG nn,A
                    case 0x92:
                        return 3;
                    // Z80N: TEST nn
                    case 0x27:
                        return 3;
                    // Z80N: ADD rr,nn
                    case 0x34: case 0x35: case 0x36:
                        return 4;
                    // Z80N: PUSH nn
                    case 0x8A:
                        return 4;
                    default:
                        return 2;
                }
            }

            // DD/FD prefix (IX/IY)
            if (op == 0xDD || op == 0xFD) {
                byte op2 = cspect.Peek((ushort)((pc + 1) & 0xFFFF));
                if (op2 == 0xCB) return 4;  // DD CB dd oo / FD CB dd oo
                switch (op2) {
                    // 2-byte (prefix only, no extra operand)
                    case 0x09: case 0x19: case 0x29: case 0x39:
                    case 0x23: case 0x2B:
                    case 0xE1: case 0xE5: case 0xE3: case 0xE9: case 0xF9:
                    case 0x44: case 0x45: case 0x4C: case 0x4D:
                    case 0x54: case 0x55: case 0x5C: case 0x5D:
                    case 0x60: case 0x61: case 0x62: case 0x63:
                    case 0x64: case 0x65: case 0x67:
                    case 0x68: case 0x69: case 0x6A: case 0x6B:
                    case 0x6C: case 0x6D: case 0x6F:
                    case 0x7C: case 0x7D:
                    case 0x84: case 0x85: case 0x8C: case 0x8D:
                    case 0x94: case 0x95: case 0x9C: case 0x9D:
                    case 0xA4: case 0xA5: case 0xAC: case 0xAD:
                    case 0xB4: case 0xB5: case 0xBC: case 0xBD:
                    case 0x24: case 0x25: case 0x2C: case 0x2D:
                        return 2;
                    // 3-byte: LD IXH/IXL,n
                    case 0x26: case 0x2E:
                        return 3;
                    // 4-byte: LD IX,nn / LD (nn),IX / LD IX,(nn)
                    case 0x21: case 0x22: case 0x2A:
                        return 4;
                    // 3-byte: (IX+d) operations
                    case 0x34: case 0x35:
                    case 0x46: case 0x4E: case 0x56: case 0x5E:
                    case 0x66: case 0x6E: case 0x7E:
                    case 0x70: case 0x71: case 0x72: case 0x73:
                    case 0x74: case 0x75: case 0x77:
                    case 0x86: case 0x8E: case 0x96: case 0x9E:
                    case 0xA6: case 0xAE: case 0xB6: case 0xBE:
                        return 3;
                    // 4-byte: LD (IX+d),n
                    case 0x36:
                        return 4;
                    default:
                        return 2;
                }
            }

            // Unprefixed instructions
            // 1-byte: handle ALU r, LD r,r, RET cc, RST, PUSH/POP ranges
            if ((op >= 0x40 && op <= 0x7F && op != 0x76) || // LD r,r (excl HALT)
                (op >= 0x80 && op <= 0xBF))                   // ALU r
                return 1;
            if ((op & 0xC7) == 0xC0) return 1; // RET cc
            if ((op & 0xC7) == 0xC7) return 1; // RST
            if ((op & 0xCF) == 0xC1) return 1; // POP rr
            if ((op & 0xCF) == 0xC5) return 1; // PUSH rr

            switch (op) {
                // Explicit 1-byte list (items not covered by ranges above)
                case 0x00: // NOP
                case 0x02: case 0x0A: case 0x12: case 0x1A:
                case 0x03: case 0x04: case 0x05: case 0x07:
                case 0x08: // EX AF,AF'
                case 0x09: case 0x0B: case 0x0C: case 0x0D: case 0x0F:
                case 0x13: case 0x14: case 0x15: case 0x17:
                case 0x19: case 0x1B: case 0x1C: case 0x1D: case 0x1F:
                case 0x23: case 0x24: case 0x25: case 0x27:
                case 0x29: case 0x2B: case 0x2C: case 0x2D: case 0x2F:
                case 0x33: case 0x34: case 0x35: case 0x37:
                case 0x39: case 0x3B: case 0x3C: case 0x3D: case 0x3F:
                case 0x76: // HALT
                case 0xC9: // RET
                case 0xD9: // EXX
                case 0xE3: // EX (SP),HL
                case 0xE9: // JP (HL)
                case 0xEB: // EX DE,HL
                case 0xF3: // DI
                case 0xF9: // LD SP,HL
                case 0xFB: // EI
                    return 1;
                // 2-byte (immediate byte)
                case 0x06: case 0x0E: case 0x16: case 0x1E:
                case 0x26: case 0x2E: case 0x36: case 0x3E:
                case 0xC6: case 0xCE: case 0xD6: case 0xDE:
                case 0xE6: case 0xEE: case 0xF6: case 0xFE:
                case 0xD3: case 0xDB: // OUT (n),A / IN A,(n)
                case 0x10: // DJNZ
                case 0x18: // JR
                case 0x20: case 0x28: case 0x30: case 0x38: // JR cc
                    return 2;
                // 3-byte (immediate word)
                case 0x01: case 0x11: case 0x21: case 0x31:
                case 0x22: case 0x2A:
                case 0x32: case 0x3A:
                case 0xC2: case 0xC3: case 0xCA: case 0xD2: case 0xDA:
                case 0xE2: case 0xEA: case 0xF2: case 0xFA:
                case 0xC4: case 0xCC: case 0xCD: case 0xD4: case 0xDC:
                case 0xE4: case 0xEC: case 0xF4: case 0xFC:
                    return 3;
                default:
                    return 1; // conservative fallback
            }
        }

        // Decode F register to an 8-character flag string (SZ5H3PNC order),
        // matching jnext's trace.cpp flags[] encoding.
        private static string DecodeFlags(int f)
        {
            char[] fl = new char[8];
            fl[0] = ((f & 0x80) != 0) ? 'S' : '-';
            fl[1] = ((f & 0x40) != 0) ? 'Z' : '-';
            fl[2] = ((f & 0x20) != 0) ? '5' : '-';
            fl[3] = ((f & 0x10) != 0) ? 'H' : '-';
            fl[4] = ((f & 0x08) != 0) ? '3' : '-';
            fl[5] = ((f & 0x04) != 0) ? 'P' : '-';
            fl[6] = ((f & 0x02) != 0) ? 'N' : '-';
            fl[7] = ((f & 0x01) != 0) ? 'C' : '-';
            return new string(fl);
        }

        public List<sIO> Init(iCSpect _cspect)
        {
            cspect = _cspect;

            // Read config from env vars.
            string envPath = Environment.GetEnvironmentVariable("JNEXT_CSPECT_TRACE_FILE");
            if (!string.IsNullOrEmpty(envPath))
                out_path = envPath;

            string envCap = Environment.GetEnvironmentVariable("JNEXT_CSPECT_TRACE_CAP");
            if (!string.IsNullOrEmpty(envCap)) {
                long parsedCap;
                if (long.TryParse(envCap, out parsedCap) && parsedCap > 0)
                    trace_cap = parsedCap;
            }

            // Task 18: skip-resets gating.
            string envSkip = Environment.GetEnvironmentVariable("JNEXT_CSPECT_TRACE_SKIP_RESETS");
            if (!string.IsNullOrEmpty(envSkip)) {
                int parsedSkip;
                if (int.TryParse(envSkip, out parsedSkip) && parsedSkip >= 0)
                    skip_resets = parsedSkip;
            }
            // If skip_resets == 0, start tracing immediately.
            if (skip_resets == 0)
                tracing_active = true;

            // Open output file immediately so data flows to disk continuously.
            try {
                string dir = Path.GetDirectoryName(out_path);
                if (!string.IsNullOrEmpty(dir))
                    Directory.CreateDirectory(dir);
                sw = new StreamWriter(out_path, append: false, encoding: Encoding.ASCII,
                                      bufferSize: 1 << 20);  // 1 MB write buffer
                sw.AutoFlush = false;
            } catch (Exception ex) {
                Console.WriteLine("CSpectFullTrace: cannot open output: " + ex.Message);
                sw = null;
            }

            Console.WriteLine(string.Format(
                "CSpectFullTrace: tracing {0} instructions -> {1} (skip {2} resets, gated={3})",
                trace_cap, out_path, skip_resets, !tracing_active));


            // Register Memory_EXE at every address 0x0000..0xFFFF.
            // This fires Read() for every M1 cycle (instruction fetch).
            var hooks = new List<sIO>(65536);
            for (int addr = 0; addr <= 0xFFFF; ++addr)
                hooks.Add(new sIO(addr, eAccess.Memory_EXE, 0, 0));

            return hooks;
        }

        public void Quit()
        {
            if (sw == null) return;
            try {
                sw.Flush();
                sw.Close();
                sw = null;
                Console.WriteLine(string.Format(
                    "CSpectFullTrace: closed. {0} instructions captured.", instr_count));
            } catch { }
        }

        public void Reset() { }
        public void Tick() { }
        public void OSTick() { }
        public bool KeyPressed(int _id) { return false; }

        public bool Write(eAccess _type, int _port, int _id, byte _value)
        {
            return false;
        }

        public byte Read(eAccess _type, int _port, int _id, out bool _isvalid)
        {
            _isvalid = false;

            // Only handle Memory_EXE (instruction fetch) events.
            if (_type != eAccess.Memory_EXE) return 0;
            if (sw == null) return 0;

            // Capture current Z80 state. GetRegs() returns state BEFORE
            // the instruction executes — matching jnext's TraceLog::record()
            // which also captures pre-execution state.
            Z80Regs r = cspect.GetRegs();
            int pc = r.PC & 0xFFFF;

            // Task 18 sync-point detection: count Z80 resets (transitions
            // INTO PC=$0000 with reset-state regs). When skip_resets reset
            // events have been observed, enable tracing.
            if (!tracing_active) {
                bool transition_to_zero = (pc == 0 && last_pc != 0);
                bool reset_state_regs = ((r.SP & 0xFFFF) == 0xFFFF);
                if (transition_to_zero && reset_state_regs) {
                    reset_count++;
                    Console.WriteLine(string.Format(
                        "CSpectFullTrace: Z80 reset #{0} detected (need {1} to start tracing)",
                        reset_count, skip_resets));
                    if (reset_count >= skip_resets) {
                        tracing_active = true;
                        Console.WriteLine("CSpectFullTrace: tracing ENABLED — capturing NextZXOS handoff");
                    }
                }
                last_pc = pc;
                if (!tracing_active) return 0;
            }
            last_pc = pc;

            // Already reached cap — ignore silently (stop tracing).
            if (instr_count >= trace_cap) return 0;

            // Determine instruction length and fetch opcode bytes.
            int len = InstrLen(pc);

            byte b0 = cspect.Peek((ushort)(pc));
            byte b1 = (len >= 2) ? cspect.Peek((ushort)((pc + 1) & 0xFFFF)) : (byte)0;
            byte b2 = (len >= 3) ? cspect.Peek((ushort)((pc + 2) & 0xFFFF)) : (byte)0;
            byte b3 = (len >= 4) ? cspect.Peek((ushort)((pc + 3) & 0xFFFF)) : (byte)0;

            string opbytes;
            switch (len) {
                case 1:  opbytes = string.Format("{0:X2}", b0); break;
                case 2:  opbytes = string.Format("{0:X2} {1:X2}", b0, b1); break;
                case 3:  opbytes = string.Format("{0:X2} {1:X2} {2:X2}", b0, b1, b2); break;
                default: opbytes = string.Format("{0:X2} {1:X2} {2:X2} {3:X2}", b0, b1, b2, b3); break;
            }

            // Decode flags from F (low byte of AF).
            string flags = DecodeFlags(r.AF & 0xFF);

            // Format line (matching jnext's snprintf format string exactly):
            // "%012llu  $%04X  AF=%04X BC=%04X DE=%04X HL=%04X"
            // "  AF'=%04X BC'=%04X DE'=%04X HL'=%04X"
            // "  IX=%04X IY=%04X SP=%04X  [%s]  %s\n"
            sw.Write(string.Format(
                "{0:D12}  ${1:X4}  AF={2:X4} BC={3:X4} DE={4:X4} HL={5:X4}" +
                "  AF'={6:X4} BC'={7:X4} DE'={8:X4} HL'={9:X4}" +
                "  IX={10:X4} IY={11:X4} SP={12:X4}  [{13}]  {14}\n",
                instr_count,         // cycle (= instruction count; not T-states)
                pc,
                r.AF & 0xFFFF, r.BC & 0xFFFF, r.DE & 0xFFFF, r.HL & 0xFFFF,
                r._AF & 0xFFFF, r._BC & 0xFFFF, r._DE & 0xFFFF, r._HL & 0xFFFF,
                r.IX & 0xFFFF, r.IY & 0xFFFF, r.SP & 0xFFFF,
                flags, opbytes));

            instr_count++;

            // Flush periodically to ensure data survives SIGKILL.
            if ((instr_count % FLUSH_EVERY) == 0) {
                sw.Flush();
                Console.WriteLine(string.Format(
                    "CSpectFullTrace: {0}/{1} instructions captured.",
                    instr_count, trace_cap));
            }

            // Return 0, _isvalid = false: do NOT consume the event.
            return 0;
        }
    }
}
