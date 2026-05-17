// Task 18 — CSpect plugin: one-shot full-state snapshot at PC=$00EF.
//
// Captures everything DZRP can't expose:
//   - Z80Regs incl. IFF1, IFF2, IM
//   - All 256 NextREGs via cspect.GetNextRegister()
//   - All 2 MiB physical SRAM via cspect.PeekPhysical()
//   - Selected eGlobals (low_rom, high_rom, div_mmc_mapping)
//
// Output format (single binary file written to JNEXT_SNAPSHOT_PATH):
//   offset 0x000..0x0FF (256B) — NextREGs $00..$FF
//   offset 0x100..0x11B  (28B)  — Z80 state:
//     0x100  u16 PC
//     0x102  u16 SP
//     0x104  u16 AF
//     0x106  u16 BC
//     0x108  u16 DE
//     0x10A  u16 HL
//     0x10C  u16 IX
//     0x10E  u16 IY
//     0x110  u16 AF'
//     0x112  u16 BC'
//     0x114  u16 DE'
//     0x116  u16 HL'
//     0x118  u8  I
//     0x119  u8  R
//     0x11A  u8  IM
//     0x11B  u8  reserved
//   offset 0x11C..0x12B (16B)  — Extended state (Task 18 plugin extension):
//     0x11C  u8  IFF1
//     0x11D  u8  IFF2
//     0x11E  u8  low_rom    (CSpect eGlobal)
//     0x11F  u8  high_rom
//     0x120  u8  div_mmc_mapping
//     0x121..0x12B reserved (11 bytes)
//   offset 0x12C..0x20012B (2 MiB) — physical SRAM, page $00 at offset 0x12C,
//                                    each page is 0x2000 bytes.
//
// Build:
//   cd tools/cspect_plugin
//   mcs -target:library \
//     -r:/home/jorgegv/src/spectrum/CSpect3_1_0_0/Plugin.dll \
//     -out:Task18Snapshot.dll Task18Snapshot.cs
//   cp Task18Snapshot.dll /home/jorgegv/src/spectrum/CSpect3_1_0_0/
//
// Env vars:
//   JNEXT_SNAPSHOT_PATH  — output binary path (default
//       /tmp/cspect_00EF_snapshot_v2.bin)
//   JNEXT_SNAPSHOT_PC    — capture PC in hex (default 00EF)
//   JNEXT_SNAPSHOT_SKIP_RESETS — wait N Z80 resets before arming the
//       BP (default 0 — for `-debug -mmc=...` mode, no firmware to skip)

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Plugin;

namespace JnextTask18
{
    public class Task18Snapshot : iPlugin
    {
        private iCSpect cspect;
        private string out_path = "/tmp/cspect_00EF_snapshot_v2.bin";
        private int capture_pc = 0x00EF;
        private int skip_resets = 0;
        private bool captured = false;
        private int reset_count = 0;
        private int last_pc = -1;

        public List<sIO> Init(iCSpect _cspect)
        {
            cspect = _cspect;

            string envPath = Environment.GetEnvironmentVariable("JNEXT_SNAPSHOT_PATH");
            if (!string.IsNullOrEmpty(envPath)) out_path = envPath;

            string envPC = Environment.GetEnvironmentVariable("JNEXT_SNAPSHOT_PC");
            if (!string.IsNullOrEmpty(envPC)) {
                int n;
                if (int.TryParse(envPC, System.Globalization.NumberStyles.HexNumber, null, out n))
                    capture_pc = n & 0xFFFF;
            }

            string envSkip = Environment.GetEnvironmentVariable("JNEXT_SNAPSHOT_SKIP_RESETS");
            if (!string.IsNullOrEmpty(envSkip)) {
                int n;
                if (int.TryParse(envSkip, out n) && n >= 0) skip_resets = n;
            }

            Console.WriteLine(string.Format(
                "Task18Snapshot: will capture at PC=${0:X4}, output to {1} (skip {2} resets)",
                capture_pc, out_path, skip_resets));

            // Hook Memory_EXE at EVERY address — CSpect's hook dispatch
            // appears to need full coverage (single-address hooks don't
            // fire reliably). We filter for capture_pc inside Read().
            var hooks = new List<sIO>(65536);
            for (int addr = 0; addr <= 0xFFFF; ++addr)
                hooks.Add(new sIO(addr, eAccess.Memory_EXE, 0, 0));
            return hooks;
        }

        public void Quit() { }
        public void Reset() { }
        public void Tick() { }
        public void OSTick() { }
        public bool KeyPressed(int _id) { return false; }
        public bool Write(eAccess _type, int _port, int _id, byte _value) { return false; }

        public byte Read(eAccess _type, int _port, int _id, out bool _isvalid)
        {
            _isvalid = false;
            if (captured) return 0;
            if (_type != eAccess.Memory_EXE) return 0;

            Z80Regs r = cspect.GetRegs();
            int pc = r.PC & 0xFFFF;

            // Skip-resets gate (if running with firmware boot first).
            if (skip_resets > 0) {
                bool transition = (pc == 0 && last_pc != 0);
                if (transition && (r.SP & 0xFFFF) == 0xFFFF) {
                    reset_count++;
                    Console.WriteLine(string.Format(
                        "Task18Snapshot: Z80 reset #{0} (need {1})",
                        reset_count, skip_resets));
                }
                last_pc = pc;
                if (reset_count < skip_resets) return 0;
            }

            if (pc != capture_pc) return 0;

            // Capture once and mark done.
            captured = true;
            Console.WriteLine(string.Format(
                "Task18Snapshot: capturing at PC=${0:X4}...", pc));

            try {
                CaptureSnapshot(r);
                Console.WriteLine("Task18Snapshot: snapshot written to " + out_path);
            } catch (Exception ex) {
                Console.WriteLine("Task18Snapshot: ERROR: " + ex.Message);
                Console.WriteLine(ex.StackTrace);
            }
            return 0;
        }

        private void CaptureSnapshot(Z80Regs r)
        {
            string dir = Path.GetDirectoryName(out_path);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);

            using (var fs = new FileStream(out_path, FileMode.Create, FileAccess.Write))
            using (var bw = new BinaryWriter(fs)) {
                // 256 NextREGs.
                byte[] nrs = new byte[256];
                for (int i = 0; i < 256; ++i) {
                    try { nrs[i] = cspect.GetNextRegister((byte)i); }
                    catch { nrs[i] = 0; }
                }
                bw.Write(nrs);

                // Z80 state (28 bytes, 12 u16 LE + 4 u8).
                bw.Write((ushort)(r.PC & 0xFFFF));
                bw.Write((ushort)(r.SP & 0xFFFF));
                bw.Write((ushort)(r.AF & 0xFFFF));
                bw.Write((ushort)(r.BC & 0xFFFF));
                bw.Write((ushort)(r.DE & 0xFFFF));
                bw.Write((ushort)(r.HL & 0xFFFF));
                bw.Write((ushort)(r.IX & 0xFFFF));
                bw.Write((ushort)(r.IY & 0xFFFF));
                bw.Write((ushort)(r._AF & 0xFFFF));
                bw.Write((ushort)(r._BC & 0xFFFF));
                bw.Write((ushort)(r._DE & 0xFFFF));
                bw.Write((ushort)(r._HL & 0xFFFF));
                bw.Write((byte)(r.I & 0xFF));
                bw.Write((byte)(r.R & 0xFF));
                bw.Write((byte)(r.IM & 0xFF));
                bw.Write((byte)0);  // reserved

                // Extended state (16 bytes).
                bw.Write((byte)(r.IFF1 ? 1 : 0));
                bw.Write((byte)(r.IFF2 ? 1 : 0));
                bw.Write(SafeGlobalByte(eGlobal.low_rom));
                bw.Write(SafeGlobalByte(eGlobal.high_rom));
                bw.Write(SafeGlobalByte(eGlobal.div_mmc_mapping));
                // 11 reserved bytes.
                for (int i = 0; i < 11; ++i) bw.Write((byte)0);

                // Physical SRAM: 256 × 8 KB = 2 MiB.
                byte[] page = new byte[0x2000];
                for (int p = 0; p < 256; ++p) {
                    for (int o = 0; o < 0x2000; ++o) {
                        try {
                            page[o] = cspect.PeekPhysical(p * 0x2000 + o);
                        } catch {
                            page[o] = 0;
                        }
                    }
                    bw.Write(page);
                }

                Console.WriteLine(string.Format(
                    "Task18Snapshot: PC=${0:X4} SP=${1:X4} AF=${2:X4} IFF1={3} IFF2={4} IM={5}",
                    r.PC, r.SP, r.AF, r.IFF1, r.IFF2, r.IM));
            }
        }

        private byte SafeGlobalByte(eGlobal g)
        {
            try {
                object o = cspect.GetGlobal(g);
                if (o == null) return 0;
                if (o is int)   return (byte)((int)o & 0xFF);
                if (o is byte)  return (byte)o;
                if (o is bool)  return (byte)(((bool)o) ? 1 : 0);
                if (o is short) return (byte)((short)o & 0xFF);
                if (o is uint)  return (byte)((uint)o & 0xFF);
                if (o is long)  return (byte)((long)o & 0xFF);
                return 0;
            } catch {
                return 0;
            }
        }
    }
}
