#!/usr/bin/env python3
"""Refresh per-row Status + Test file:line columns in
`doc/testing/TRACEABILITY-MATRIX.md` for the 9 Task 1 refactored subsystems.

Strategy (format-agnostic across test harnesses):

1. For each test source file, grep for `check("ID", ...)` and `skip("ID", ...)`
   first-arg string literals. These are the ground truth for which plan row
   IDs the test file exercises and whether as a live check or an honest skip.
2. For each test binary, run it and collect only the **FAIL** set — this is
   the one output line ("  FAIL ID: ...") that every harness agrees on.
3. Derive per-ID status:
     - `fail` if ID is in the binary's FAIL set
     - `skip` if ID is a `skip()` call in the source AND not in FAIL set
     - `pass` if ID is a `check()` call in the source AND not in FAIL set
     - `missing` if ID is not found in the source at all
4. Edit the matrix in place: for each data row whose first cell is a test ID,
   rewrite the Status cell and the Test file:line cell preserving column
   widths. Section boundaries are matched by exact header line.

Usage:
    python3 test/refresh-traceability-matrix.py

Dependencies: the 9 test binaries must already be built under `build/test/`
before running this script — the script does NOT build them. Run from repo
root or anywhere; paths are absolute.

See `doc/testing/UNIT-TEST-PLAN-EXECUTION.md` §6a for the broader matrix
refresh workflow this script implements."""

import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MATRIX = ROOT / "doc/testing/TRACEABILITY-MATRIX.md"

# (section_header_line, test_binary, source_rel_path)
# Every non-Z80N subsystem with per-row `check()`/`skip()` tracking.
# Z80N is deliberately excluded: it uses the FUSE data-driven runner and
# its per-row status is permanently `missing` by design (see §6a).
SUBSYS = [
    ("## Memory/MMU — `test/mmu/mmu_test.cpp`",
     "build/test/mmu_test", "test/mmu/mmu_test.cpp"),
    ("## ULA Video — `test/ula/ula_test.cpp`",
     "build/test/ula_test", "test/ula/ula_test.cpp"),
    ("## Layer2 — `test/layer2/layer2_test.cpp`",
     "build/test/layer2_test", "test/layer2/layer2_test.cpp"),
    ("## Sprites — `test/sprites/sprites_test.cpp`",
     "build/test/sprites_test", "test/sprites/sprites_test.cpp"),
    ("## Tilemap — `test/tilemap/tilemap_test.cpp`",
     "build/test/tilemap_test", "test/tilemap/tilemap_test.cpp"),
    ("## Copper — `test/copper/copper_test.cpp`",
     "build/test/copper_test", "test/copper/copper_test.cpp"),
    ("## Compositor — `test/compositor/compositor_test.cpp`",
     "build/test/compositor_test", "test/compositor/compositor_test.cpp"),
    ("## Audio — `test/audio/audio_test.cpp`",
     "build/test/audio_test", "test/audio/audio_test.cpp"),
    ("## DMA — `test/dma/dma_test.cpp`",
     "build/test/dma_test", "test/dma/dma_test.cpp"),
    ("## DivMMC+SPI — `test/divmmc/divmmc_test.cpp`",
     "build/test/divmmc_test", "test/divmmc/divmmc_test.cpp"),
    ("## CTC+Interrupts — `test/ctc/ctc_test.cpp`",
     "build/test/ctc_test", "test/ctc/ctc_test.cpp"),
    ("## UART+I2C/RTC — `test/uart/uart_test.cpp`",
     "build/test/uart_test", "test/uart/uart_test.cpp"),
    ("## NextREG — `test/nextreg/nextreg_test.cpp`",
     "build/test/nextreg_test", "test/nextreg/nextreg_test.cpp"),
    ("## IO Port Dispatch — `test/port/port_test.cpp`",
     "build/test/port_test", "test/port/port_test.cpp"),
    ("## Input — `test/input/input_test.cpp`",
     "build/test/input_test", "test/input/input_test.cpp"),
]

# "  FAIL ID: ..." or "  FAIL ID [..." — robust across all known harnesses.
FAIL_RE = re.compile(r"^\s*FAIL\s+([A-Za-z0-9._\-]+)\s*[:\[]")

# skip("ID", ...) or stub("ID", ...) — first-arg string literal. Both helpers
# flag "not reachable via current C++ API" and are aggregated under the
# Skip/Stub column in the Summary table. Always direct calls (no loop
# indirection) so a literal-match regex is sufficient.
SKIP_RE = re.compile(r'\b(?:skip|stub)\s*\(\s*"([A-Za-z0-9._\-]+)"')

# Plan-row-shaped string literal anywhere in the source. Captures IDs used
# both directly in check() calls AND indirectly via struct-of-rows loops
# where the ID is embedded in an aggregate initializer and later passed
# via `r.id`. Matches three common shapes seen across subsystems:
#
#   1. Dashed prefix:        "MMU-01", "AY-110", "TM-CB5", "I2C-P05a",
#                            "G1.AT-01", "G10.SC-01" (group-prefixed),
#                            "S1.05-mode" etc.
#   2. Numeric dotted:       "9.7", "14.6", "14.7a" (DMA plan rows)
#   3. Section-dotted:       "S13.14", "S2.08" (ULA sections)
#
# False positives (English strings etc.) are minimal in test files because
# the shape is distinctive. Log/fmt strings rarely match.
ID_LITERAL_RE = re.compile(
    r'"('
    r'[A-Z][A-Z0-9]*(?:\.[A-Z][A-Z0-9]*)*-[A-Za-z0-9._\-+]+'  # dashed (+ allowed for collapsed rows like REG-06+07)
    r'|\d+\.\d+[a-z]?'                                        # numeric like 9.7 / 14.7a
    r'|S\d+\.\d+[a-z]?'                                       # ULA S-section
    r')"'
)

def run_fails(binary):
    """Run binary, return set of failing plan row IDs."""
    p = subprocess.run([str(ROOT / binary)],
                       capture_output=True, text=True, timeout=180)
    out = p.stdout + "\n" + p.stderr
    fails = set()
    for line in out.splitlines():
        m = FAIL_RE.match(line)
        if m:
            fails.add(m.group(1))
    return fails

def grep_source(source_rel):
    """Return (checks, skips) dicts {id: first_line_number} from source file.

    skips = ids appearing as `skip("ID", ...)` first-arg literals.
    checks = all other plan-ID-shaped literals found anywhere in the source
             (covers direct `check("ID", ...)` calls AND struct-of-rows loops
             where the ID is embedded in an aggregate initializer and later
             passed via `r.id`). Line number is the first occurrence."""
    checks, skips = {}, {}
    text = (ROOT / source_rel).read_text()
    for lineno, line in enumerate(text.splitlines(), start=1):
        for m in SKIP_RE.finditer(line):
            skips.setdefault(m.group(1), lineno)
    for lineno, line in enumerate(text.splitlines(), start=1):
        for m in ID_LITERAL_RE.finditer(line):
            tid = m.group(1)
            if tid in skips:
                continue
            checks.setdefault(tid, lineno)
    return checks, skips

SUBLETTERS = ["a", "b", "c"]

def resolve_ids(tid, checks, skips):
    """Return the list of concrete source IDs that cover this matrix row.
    If tid is found directly, returns [tid]. Otherwise tries sub-letter
    variants `tid + 'a'`, `tid + 'b'`, `tid + 'c'` — these appear when the
    author split one plan row into multiple assertions (e.g. I2C-P05 →
    I2C-P05a/b). Returns [] if nothing matches."""
    if tid in checks or tid in skips:
        return [tid]
    variants = [tid + s for s in SUBLETTERS
                if (tid + s) in checks or (tid + s) in skips]
    return variants

def status_for(tid, fails, checks, skips):
    resolved = resolve_ids(tid, checks, skips)
    if not resolved:
        return "missing"
    # Aggregate across sub-letter variants: fail wins over skip wins over pass.
    any_fail = any(r in fails for r in resolved)
    if any_fail:
        return "fail"
    any_skip = any(r in skips for r in resolved)
    if any_skip and all(r in skips for r in resolved):
        return "skip"
    return "pass"

def line_for(tid, checks, skips):
    resolved = resolve_ids(tid, checks, skips)
    for r in resolved:
        if r in checks:
            return checks[r]
        if r in skips:
            return skips[r]
    return None

def refresh_section(lines, start_idx, binary, source_rel):
    """Edit `lines` in place from start_idx+1 up to next '## ' heading.
    Returns (rows_touched, (pass, fail, skip, missing))."""
    fails = run_fails(binary)
    checks, skips = grep_source(source_rel)

    pass_ct = fail_ct = skip_ct = missing_ct = 0
    i = start_idx + 1
    touched = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("## ") and i > start_idx + 1:
            break
        if line.startswith("| ") and "|" in line[2:]:
            cells = line.split("|")
            # cells: ['', ' ID ', ' title ', ' vhdl ', ' status ', ' file:line ', '']
            if len(cells) >= 7:
                tid_raw = cells[1].strip()
                # Skip header and separator rows
                if tid_raw and tid_raw != "Test ID" and not set(tid_raw) <= set("-: "):
                    new_status = status_for(tid_raw, fails, checks, skips)
                    if new_status == "pass": pass_ct += 1
                    elif new_status == "fail": fail_ct += 1
                    elif new_status == "skip": skip_ct += 1
                    else: missing_ct += 1

                    # Preserve column widths exactly
                    orig_status = cells[4]
                    width = len(orig_status) - 2  # strip the two space padders
                    cells[4] = " " + new_status.ljust(width) + " "

                    ln = line_for(tid_raw, checks, skips)
                    if ln is not None:
                        location = f"{source_rel}:{ln}"
                    else:
                        location = "missing"
                    orig_loc = cells[5]
                    loc_width = len(orig_loc) - 2
                    cells[5] = " " + location[:loc_width].ljust(loc_width) + " "

                    lines[i] = "|".join(cells)
                    touched += 1
        i += 1
    return touched, (pass_ct, fail_ct, skip_ct, missing_ct)

def main():
    text = MATRIX.read_text()
    lines = text.splitlines(keepends=False)

    report = []
    for header, binary, source_rel in SUBSYS:
        try:
            idx = next(i for i, l in enumerate(lines) if l.strip() == header)
        except StopIteration:
            print(f"NOT FOUND: {header}")
            continue
        touched, tally = refresh_section(lines, idx, binary, source_rel)
        report.append((header, touched, *tally))

    MATRIX.write_text("\n".join(lines) + "\n")

    print(f"\n{'Subsystem':<22} {'rows':>5} {'pass':>5} {'fail':>5} {'skip':>5} {'miss':>5}")
    print("-" * 52)
    totals = [0, 0, 0, 0, 0]
    for header, touched, p, f, s, m in report:
        short = header.replace("## ", "").split(" — ")[0]
        print(f"{short:<22} {touched:>5} {p:>5} {f:>5} {s:>5} {m:>5}")
        totals[0] += touched; totals[1] += p; totals[2] += f; totals[3] += s; totals[4] += m
    print("-" * 52)
    print(f"{'TOTAL':<22} {totals[0]:>5} {totals[1]:>5} {totals[2]:>5} {totals[3]:>5} {totals[4]:>5}")

if __name__ == "__main__":
    main()
