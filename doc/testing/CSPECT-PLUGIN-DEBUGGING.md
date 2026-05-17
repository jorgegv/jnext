# CSpect plugin for differential debugging

**Task 16 verdict (2026-05-16):** writing a CSpect plugin to instrument CSpect execution the same way we instrument jnext is **feasible, low-friction, and proven decisive in practice**. We already have a working 437-line C# plugin (`tools/cspect_plugin/JnextG46bTrace.cs` on the `g46b-investigation-v2` branch) that has been used as the primary diagnostic tool in the G46(b) investigation series, EOD-30i+6 through EOD-30i+25e.

## Why this matters

The G46(b) investigation needs CSpect-vs-jnext differential debugging — same SD image, same NEX, capture per-event physical state in both and diff. Two host-side mechanisms exist on the CSpect side:

1. **DZRP debugger client** (`tools/cspect_dzrp/`) — works but every breakpoint halts the CPU, which **perturbs timing**. Pulse-window / IRQ-delivery investigations have repeatedly produced bad data because BPs at $0038 stretched the ISR enough to mask the real divergence (EOD-30i+5 "BP-perturbation finding").
2. **iPlugin C# .NET plugin** — runs inside the CSpect process, hooks port/memory events without halting the CPU. Zero perturbation. Full access to CSpect's `iCSpect` API (`Peek`, `PeekPhysical`, `GetRegs`, NextReg readback).

The plugin path is strictly better for any investigation where CPU timing matters. The DZRP path remains useful for one-shot state dumps where timing doesn't matter (e.g. read SRAM page X at HALT).

## Hook surface (proven in `JnextG46bTrace.cs`)

The CSpect `iPlugin` interface exposes:

```csharp
public interface iPlugin
{
    List<sIO>   Init(iCSpect _CSpect);   // declare hooks; capture iCSpect handle
    void        Quit();                  // teardown (flush logs etc.)
    void        Reset();                 // CSpect reset
    void        Tick();                  // called every CPU tick → time-based events
    void        OSTick();                // OS-time ticks (~50 Hz)
    bool        KeyPressed(int _id);
    bool        Write(eAccess _type, int _port, int _id, byte _value);   // port write / mem write / mem EXE
    byte        Read (eAccess _type, int _port, int _id, out bool _isvalid);
}
```

Hooks the plugin can register (subset, all observed working):

- `eAccess.Port_Read` / `Port_Write` at any 16-bit port address.
- `eAccess.Memory_Read` / `Memory_Write` at any 16-bit address (CPU view).
- `eAccess.Memory_EXE` at any PC — fires on M1 fetch. Used in `JnextG46bTrace` to capture every PC visit in $0000-$1FFF + curated supervisor PCs ($14C0, $0E93, $07AA, etc.).

The `iCSpect` API exposes:

- `Peek(ushort addr)` — CPU-view byte read (honours current MMU + DivMMC overlays).
- `PeekPhysical(int addr)` — direct SRAM byte (bypasses MMU). Lets us snapshot raw RAM pages.
- `GetRegs()` — full Z80 register state (AF/BC/DE/HL + AF'/BC'/DE'/HL' + IX/IY/SP/PC + I/R + IFF1/2 + IM).
- `GetNextRegister(byte n)` — NextReg readback (NR $00..$FF).
- Various other introspection (`GetWaveAY`, `DebuggerEnable`, etc.).

## Build pipeline

```bash
# Build (Mono mcs):
mcs -target:library \
    -r:/path/to/CSpect3_1_0_0/Plugin.dll \
    -out:JnextG46bTrace.dll \
    JnextG46bTrace.cs

# Install:
cp JnextG46bTrace.dll /path/to/CSpect3_1_0_0/
# CSpect auto-loads .dlls from its install directory at startup.
```

Mono is required on Linux (CSpect itself runs under Mono). The build is independent of jnext's CMake pipeline — no build-system integration needed.

## Evidence the approach works

From the G46(b) session handovers (`project_session_handover_2026-05-15*.md`) and `doc/issues/nextzxos-boot/g46b-v2/EOD-30i-real-hardware-test.md`:

- **18 795 port-write events captured in ~15 s of real-time CSpect boot**, with zero CPU perturbation. The plugin observed CSpect's full SD-via-$E3 traffic at full speed.
- **Decisive root-cause finding (G46(b)-v2 EOD-30i+6)**: the plugin's per-event `PeekPhysical` of all 16 DivMMC SRAM banks at every $E3 write proved that CSpect's $2331 maps to main RAM while jnext's maps to DivMMC SRAM bank 0 — a discovery that DZRP BPs had repeatedly failed to make because the BP-induced timing skew masked it (the EOD-30i+5 "BP-perturbation finding"). See the handover series indexed in [MEMORY.md](file:///home/jorgegv/.claude/projects/-home-jorgegv-src-spectrum-jnext/memory/MEMORY.md) for the per-EOD breakdown; `EOD-30i-real-hardware-test.md:56-58` explicitly credits the plugin as the "most decisive measurement" of that work.
- **Boot ROM dump at `Init()` time** captured CSpect's $0000-$1FFF view to a reference binary (`tools/cspect_plugin/cspect_bootrom_cpu_view.bin`), which fed the "different boot ROMs" diagnosis (EOD-30i+14).
- **Cross-emulator binary diffs** (`tools/cspect_plugin/cspect_slots01_at_*.bin`) used as ground truth for jnext-side `JNEXT_PATCH_ROM_BANK0_WITH_IPL` experiments.

## Verdict

| Dimension | Verdict |
| --- | --- |
| Feasibility | **Yes.** `iPlugin` interface is well-documented (mikedailly/CSpectPlugins repo + working example), Mono builds in seconds, plugins auto-load. |
| Effort | **S–M.** First plugin from scratch took roughly a day of exploration; subsequent extensions added in 30–60 min increments. |
| Coverage parity with jnext probes | **Yes.** Plugin hooks cover the same surface jnext's env-gated probes cover: port reads/writes, memory writes, PC fetches, register state, MMU page reads. |
| Perturbation | **Zero.** Plugin runs inside CSpect's event loop; no BP halt, no DZRP round-trip. |
| Bug-finding leverage | **Proven decisive.** EOD-30i+6 root-cause identification would not have been possible without it. |

**Recommendation:** keep `tools/cspect_plugin/` on the `g46b-investigation-v2` branch as the canonical CSpect-side instrumentation; merge it to `main` whenever the G46(b) work itself merges. Use the plugin (not DZRP) for any new differential investigation where CPU timing matters. Reserve DZRP for one-shot HALTed state dumps.

## Independent assessment

This verdict was reviewed by an independent agent on 2026-05-16; both the methodology claim (plugin > DZRP for timing-sensitive diffs) and the specific evidence cited (EOD-30i+6, 18 795 events, zero perturbation) were verified against the source plugin and the EOD-30 doc series.
