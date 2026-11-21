#!/usr/bin/env python3
"""Show a jnext bug report (File > Save Bug Report..., or --bug-report) on screen.

    jnext-bug-view.py REPORT [-o OUT.html] [--text [--memory]] [--no-open]

By default the report becomes one self-contained HTML page (no JavaScript, no external
resource; the pictures are embedded) written to a temporary folder, or to OUT.html, and
opened in the browser unless --no-open is given. --text prints the same sections as plain
text instead; --memory adds the hex dump of the CPU's 64 KB view when the report has memory.

Exit status: 0 the report is shown, 2 the file is refused (not a jnext bug report, a newer
format, a declared member missing, or a size limit exceeded), 1 an I/O error.

The viewer needs only Python 3 and the report: no jnext build, no SD card image. It decodes
nothing itself: register names, NextREG names, the paging map and the disassembly are what
jnext wrote into the report. A report comes from an issue attachment, so it is UNTRUSTED:
every text is escaped, a picture is embedded only if it is a PNG, nothing is extracted to
disk, and a member that declares more than 64 MiB (or a total above 256 MiB) is refused
before it is read. Well-formed JSON of the wrong shape shows its section as absent, and --text
writes every control character of the report (escape sequences included) as \\xNN.
"""
import argparse
import base64
import html
import json
import os
import sys
import tempfile
import webbrowser
import zipfile
import zlib

KIND = "jnext-bug-report"
SUPPORTED_FORMAT = 1
MAX_MEMBER = 64 * 1024 * 1024
MAX_TOTAL = 256 * 1024 * 1024
MAX_ENTRIES = 4096
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
LAYER_VIEWS = ["ula-primary", "ula-shadow", "layer2-active", "layer2-shadow", "sprites", "tilemap", "background"]


def sanitize(text):
    """`text` with every character a terminal could act on written as \\xNN: C0 controls except newline
    and tab, DEL and the C1 controls (an OSC or CSI sequence in a hostile report is only text)."""
    out = []
    for c in text:
        o = ord(c)
        if c in "\n\t" or (o >= 0x20 and not 0x7F <= o <= 0x9F):
            out.append(c)
        else:
            out.append("\\x%02x" % o)
    return "".join(out)


# What a section shows when the report holds data of an unexpected shape (well-formed JSON of the wrong
# type): the section is absent, never a crash.
SHAPE_ERRORS = (TypeError, AttributeError, KeyError, ValueError, IndexError, OverflowError, RecursionError)


def guarded(ids_titles, fn, rep):
    try:
        return fn(rep)
    except SHAPE_ERRORS:
        return "\n".join(section(i, t, "<p><em>%s: the report holds data of an unexpected shape, not shown.</em></p>" % E(t))
                         for i, t in ids_titles)


class Refused(Exception):
    """The file is not a report this viewer will show."""


class Report:
    def __init__(self, path):
        self.path = path
        try:
            self.zip = zipfile.ZipFile(path)
        except zipfile.BadZipFile:
            raise Refused("not a zip file")
        except OSError:
            raise
        infos = self.zip.infolist()
        if len(infos) > MAX_ENTRIES:
            raise Refused("more than %d members" % MAX_ENTRIES)
        total = 0
        self.sizes = {}
        for i in infos:
            if i.file_size > MAX_MEMBER:
                raise Refused("member %r declares %d bytes, over the %d MiB limit" % (i.filename, i.file_size, MAX_MEMBER >> 20))
            total += i.file_size
            self.sizes[i.filename] = i.file_size
        if total > MAX_TOTAL:
            raise Refused("the members declare %d bytes in all, over the %d MiB limit" % (total, MAX_TOTAL >> 20))
        if "manifest.json" not in self.sizes:
            raise Refused("no manifest.json: not a jnext bug report")
        self.manifest = self.json("manifest.json", required=True)
        if not isinstance(self.manifest, dict):
            raise Refused("manifest.json is not an object")
        if self.manifest.get("kind") != KIND:
            raise Refused("manifest.json kind is %r, not %r: not a jnext bug report" % (self.manifest.get("kind"), KIND))
        ver = self.manifest.get("format_version")
        if not isinstance(ver, int) or isinstance(ver, bool) or ver < 1:
            raise Refused("manifest.json has no usable format_version")
        if ver > SUPPORTED_FORMAT:
            raise Refused("format_version %d is newer than this viewer understands (%d): use a newer jnext-bug-view.py" % (ver, SUPPORTED_FORMAT))
        members = self.manifest.get("members", [])
        if not isinstance(members, list):
            raise Refused("manifest.json is malformed: members is not a list")
        for m in members:
            name = m.get("name") if isinstance(m, dict) else None
            if not isinstance(name, str):
                raise Refused("manifest.json lists a member with no name")
            if name not in self.sizes:
                raise Refused("declared member missing: %r" % name)

    def has(self, name):
        return name in self.sizes

    def raw(self, name):
        """The bytes of a member, never more than it declared (and never more than the cap)."""
        if name not in self.sizes:
            return None
        with self.zip.open(name) as f:
            data = f.read(MAX_MEMBER + 1)
        if len(data) > MAX_MEMBER:
            raise Refused("member %r inflates past the %d MiB limit" % (name, MAX_MEMBER >> 20))
        return data

    def text(self, name):
        data = self.raw(name)
        return None if data is None else data.decode("utf-8", "replace")

    def json(self, name, required=False):
        data = self.raw(name)
        if data is None:
            return None
        try:
            return json.loads(data.decode("utf-8", "replace"))
        except (ValueError, RecursionError):
            if required:
                raise Refused("%s is not valid JSON" % name)
            return None


# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

def E(x):
    return html.escape(str(x), quote=True)


WIDE_REGISTERS = {"AF", "BC", "DE", "HL", "AF2", "BC2", "DE2", "HL2", "IX", "IY", "SP", "PC", "MEMPTR"}


def reg_digits(name):
    """Hex digits of a register: the 16-bit ones are always four."""
    return 4 if name in WIDE_REGISTERS else 2


def hexn(v, digits=2):
    return ("0x%0*X" % (digits, v)) if isinstance(v, int) and not isinstance(v, bool) else str(v)


def hex_rows(data, base=0, width=16):
    """Yield (address, hex text, ascii text) per row; a block of 256 zero bytes is one row."""
    i = 0
    n = len(data)
    while i < n:
        block = data[i:i + 256]
        if len(block) == 256 and not any(block):
            yield base + i, None, None
            i += 256
            continue
        row = data[i:i + width]
        yield (base + i,
               " ".join("%02X" % b for b in row),
               "".join(chr(b) if 32 <= b < 127 else "." for b in row))
        i += width


def hex_dump_text(data, base=0):
    out = []
    for addr, hx, asc in hex_rows(data, base):
        if hx is None:
            out.append("$%04X..$%04X: 256 zero bytes" % (addr, addr + 255))
        else:
            out.append("$%04X: %-47s |%s|" % (addr, hx, asc))
    return "\n".join(out)


def memory_state(rep):
    mem = rep.manifest.get("memory", {})
    if isinstance(mem, dict) and mem.get("included") is True and rep.has("memory/cpu.bin"):
        return True, ""
    w = mem.get("withheld", {}) if isinstance(mem, dict) else {}
    reason = w.get("reason", "") if isinstance(w, dict) else ""
    code = w.get("code", "") if isinstance(w, dict) else ""
    return False, "%s%s" % (reason, (" (%s)" % code) if code else "")


# ---------------------------------------------------------------------------
# HTML
# ---------------------------------------------------------------------------

def table(rows, header=None):
    out = ["<table>"]
    if header:
        out.append("<tr>" + "".join("<th>%s</th>" % E(h) for h in header) + "</tr>")
    for r in rows:
        out.append("<tr>" + "".join("<td>%s</td>" % E(c) for c in r) + "</tr>")
    out.append("</table>")
    return "\n".join(out)


def kv_table(d):
    if not isinstance(d, dict):
        return "<p>%s</p>" % E(d)
    return table([(k, json.dumps(v) if isinstance(v, (dict, list)) else v) for k, v in d.items()], ["field", "value"])


def section(id_, title, body):
    return '<section id="%s"><h2>%s</h2>\n%s\n</section>' % (id_, E(title), body)


def absent(what):
    return "<p><em>%s: not in this report.</em></p>" % E(what)


def html_summary(rep):
    m = rep.manifest
    p = m.get("producer", {}) if isinstance(m.get("producer"), dict) else {}
    rows = [
        ("jnext version", p.get("jnext_version", "")), ("platform", p.get("platform", "")),
        ("operating system", "%s %s" % (p.get("os", ""), p.get("os_release", ""))),
        ("architecture", p.get("arch", "")), ("frontend", p.get("frontend", "")),
        ("toolkit", p.get("toolkit", "")), ("machine", m.get("machine", "")),
        ("created (UTC)", m.get("created_utc", "")), ("format version", m.get("format_version", "")),
    ]
    out = [table(rows, ["", ""])]
    cards = m.get("cards", [])
    out.append("<h3>SD cards</h3>" + (table([(c.get("slot"), c.get("name"), c.get("bytes"), c.get("sha256"), c.get("readonly"))
                                              for c in cards if isinstance(c, dict)],
                                             ["slot", "name", "bytes", "SHA-256", "read-only"]) if cards else "<p>none</p>"))
    progs = m.get("programs", [])
    rows = []
    for g in progs:
        if not isinstance(g, dict):
            continue
        w = g.get("withheld") if isinstance(g.get("withheld"), dict) else {}
        rows.append((g.get("name"), g.get("bytes"), g.get("sha256", ""),
                     "included" if g.get("included") else "withheld: %s: %s" % (w.get("code", ""), w.get("reason", ""))))
    out.append("<h3>Programs</h3>" + (table(rows, ["name", "bytes", "SHA-256", "state"]) if rows else "<p>none</p>"))
    ok, why = memory_state(rep)
    out.append("<h3>Memory</h3><p>%s</p>" % (E("included") if ok else E("not included: " + why)))
    warns = m.get("warnings", [])
    out.append("<h3>Warnings</h3>" + ("<ul>%s</ul>" % "".join("<li>%s</li>" % E(w) for w in warns) if warns else "<p>none</p>"))
    return section("summary", "Summary", "\n".join(out))


def html_settings(rep):
    s = rep.json("settings.json")
    if not isinstance(s, dict):
        return section("settings", "Settings", absent("settings.json"))
    out = ["<h3>Command line</h3><pre>%s</pre>" % E(" ".join(str(a) for a in s.get("command_line", [])))]
    cf = s.get("config_file")
    out.append("<h3>Settings file</h3>" + (kv_table(cf) if isinstance(cf, dict) else "<p>%s</p>" % E(cf)))
    out.append("<h3>Session</h3>" + kv_table(s.get("session", {})))
    return section("settings", "Settings", "\n".join(out))


def html_machine(rep):
    m = rep.json("machine.json")
    if not isinstance(m, dict):
        return "\n".join(section(i, t, absent("machine.json")) for i, t in
                         [("machine", "Machine"), ("nextregs", "NextREGs"), ("paging", "Paging"),
                          ("video", "Video"), ("interrupts", "Interrupts")])
    parts = []
    regs = m.get("registers", {})
    body = table([(k, hexn(int(v) if isinstance(v, bool) else v, reg_digits(k)),
                   int(v) if isinstance(v, bool) else v) for k, v in regs.items()],
                 ["register", "hex", "decimal"]) if isinstance(regs, dict) else absent("registers")
    body += "<h3>Other</h3>"
    body += kv_table({k: m[k] for k in ("divmmc", "multiface", "audio") if k in m})
    parts.append(section("machine", "Machine", body))
    rows = []
    for e in m.get("nextregs", []):
        if not isinstance(e, dict):
            continue
        v = e.get("value")
        rows.append(("0x%02X" % e["reg"] if isinstance(e.get("reg"), int) else e.get("reg"), e.get("name"),
                     hexn(v, 2), ("{:08b}".format(v) if isinstance(v, int) and 0 <= v <= 255 else "")))
    parts.append(section("nextregs", "NextREGs", table(rows, ["reg", "name", "hex", "binary"]) if rows else absent("nextregs")))
    pg = m.get("paging", {})
    if isinstance(pg, dict):
        srows = []
        for s in pg.get("slots", []):
            if isinstance(s, dict):
                sp = s.get("space", {}) if isinstance(s.get("space"), dict) else {}
                nw = s.get("now", {}) if isinstance(s.get("now"), dict) else {}
                srows.append((s.get("slot"), s.get("nr_page"), s.get("effective_page"), s.get("is_rom"),
                              "%s %s" % (sp.get("kind"), sp.get("index")), "%s %s" % (nw.get("kind"), nw.get("index"))))
        body = table(srows, ["slot", "NR page", "effective page", "ROM", "space", "reads now"])
        body += "<h3>Ports</h3>" + kv_table(pg.get("ports", {})) + "<h3>ROM select</h3>" + kv_table(pg.get("rom_select", {}))
    else:
        body = absent("paging")
    parts.append(section("paging", "Paging", body))
    v = m.get("video", {})
    if isinstance(v, dict):
        body = []
        for k in ("raster", "time", "machine", "ula", "clip", "sprite_clip", "copper"):
            if k in v:
                body.append("<h3>%s</h3>%s" % (E(k), kv_table(v[k])))
        sp = [s for s in v.get("sprites", []) if isinstance(s, dict) and s.get("visible")]
        body.append("<h3>Sprites (%d visible of %d)</h3>" % (len(sp), len(v.get("sprites", []))))
        if sp:
            keys = list(sp[0].keys())
            body.append(table([[s.get(k) for k in keys] for s in sp], keys))
        pal = v.get("palettes", {})
        if isinstance(pal, dict):
            body.append("<h3>Palettes</h3>")
            for name, p in pal.items():
                rgb = p.get("rgb333", []) if isinstance(p, dict) else []
                body.append("<details><summary>%s</summary><pre>%s</pre></details>" % (
                    E(name), E(" ".join("%03X" % x for x in rgb if isinstance(x, int)))))
        body = "\n".join(body)
    else:
        body = absent("video")
    parts.append(section("video", "Video", body))
    it = m.get("interrupts", {})
    if isinstance(it, dict):
        body = kv_table({k: x for k, x in it.items() if k != "nextregs"})
        body += table([(("0x%02X" % e["reg"]) if isinstance(e.get("reg"), int) else e.get("reg"), e.get("name"), hexn(e.get("value"), 2))
                       for e in it.get("nextregs", []) if isinstance(e, dict)], ["reg", "name", "value"])
    else:
        body = absent("interrupts")
    parts.append(section("interrupts", "Interrupts", body))
    return "\n".join(parts)


def html_pictures(rep):
    out = []
    names = ["screen/composite.png"] + ["screen/%s.png" % v for v in LAYER_VIEWS]
    for n in names:
        data = rep.raw(n) if rep.has(n) else None
        if data is None:
            continue
        if not data.startswith(PNG_SIGNATURE):
            out.append("<p>%s: not a PNG, not shown.</p>" % E(n))
            continue
        out.append("<figure><figcaption>%s</figcaption><img alt=\"%s\" src=\"data:image/png;base64,%s\"></figure>" % (
            E(n), E(n), base64.b64encode(data).decode("ascii")))
    return section("pictures", "Pictures", "\n".join(out) if out else absent("pictures"))


def html_log(rep):
    t = rep.text("log.txt")
    return section("log", "Log", ("<pre>%s</pre>" % E(t)) if t is not None else absent("log.txt"))


def html_debugger(rep):
    d = rep.json("debugger.json")
    if not isinstance(d, dict):
        return section("debugger", "Debugger", absent("debugger.json"))
    out = ["<h3>Run state</h3>" + kv_table(d.get("run_state", {}))]
    for k in ("pc_symbol", "pc_source", "pc_space"):
        if k in d:
            out.append("<h3>%s</h3>%s" % (E(k), kv_table(d[k]) if isinstance(d[k], dict) else "<p>%s</p>" % E(d[k])))
    bps = d.get("breakpoints", [])
    out.append("<h3>Breakpoints</h3>" + (table([[b.get(k) if not isinstance(b.get(k), dict) else json.dumps(b.get(k)) for k in bps[0]]
                                                for b in bps if isinstance(b, dict)], list(bps[0].keys()))
                                         if bps and isinstance(bps[0], dict) else "<p>none</p>"))
    cs = d.get("call_stack", {})
    frames = cs.get("frames", []) if isinstance(cs, dict) else []
    out.append("<h3>Call stack</h3>" + (table([[f.get(k) if not isinstance(f.get(k), dict) else json.dumps(f.get(k)) for k in frames[0]]
                                                for f in frames if isinstance(f, dict)], list(frames[0].keys()))
                                         if frames and isinstance(frames[0], dict) else "<p>empty</p>"))
    tr = d.get("trace", {})
    ent = tr.get("entries", []) if isinstance(tr, dict) else []
    keys = ["cycle", "pc", "af", "bc", "de", "hl", "sp", "disasm", "opcode_bytes"]
    out.append("<h3>Trace (%d entries)</h3>" % len(ent))
    if ent:
        out.append(table([[json.dumps(e.get(k)) if isinstance(e.get(k), (list, dict)) else e.get(k, "") for k in keys]
                          for e in ent if isinstance(e, dict)], keys))
    if "disasm_at_pc" in d:
        out.append("<h3>Disassembly at PC</h3><pre>%s</pre>" % E(d["disasm_at_pc"]))
    if "stack" in d:
        out.append("<h3>Stack</h3>" + kv_table(d["stack"]))
    return section("debugger", "Debugger", "\n".join(out))


def html_memory(rep):
    ok, why = memory_state(rep)
    if not ok:
        return section("memory", "Memory", "<p>not included: %s</p>" % E(why))
    out = []
    cpu = rep.raw("memory/cpu.bin")
    out.append("<details><summary>CPU view (64 KB)</summary><pre>%s</pre></details>" % E(hex_dump_text(cpu)))
    ram = rep.raw("memory/ram.bin") if rep.has("memory/ram.bin") else b""
    for p in range(len(ram) // 8192):
        page = ram[p * 8192:(p + 1) * 8192]
        if not any(page):
            out.append("<p>page 0x%02X: all zero</p>" % p)
        else:
            out.append("<details><summary>page 0x%02X (8 KB)</summary><pre>%s</pre></details>" % (p, E(hex_dump_text(page))))
    dis = rep.text("memory/cpu-disasm.txt")
    if dis is not None:
        out.append("<details><summary>Disassembly of the CPU view</summary><pre>%s</pre></details>" % E(dis))
    return section("memory", "Memory", "\n".join(out))


def html_raw_state(rep):
    out = []
    for n in sorted(rep.sizes):
        if n.startswith("state/") and n.endswith(".json"):
            t = rep.text(n)
            out.append("<details><summary>%s</summary><pre>%s</pre></details>" % (E(n), E(t)))
    return section("raw-state", "Raw state", "\n".join(out) if out else absent("state documents"))


def render_html(rep):
    title = "jnext bug report"
    G = lambda ids, fn: guarded(ids, fn, rep)
    body = "\n".join([
        G([("summary", "Summary")], html_summary), G([("settings", "Settings")], html_settings),
        G([("machine", "Machine"), ("nextregs", "NextREGs"), ("paging", "Paging"), ("video", "Video"),
           ("interrupts", "Interrupts")], html_machine),
        G([("pictures", "Pictures")], html_pictures), G([("log", "Log")], html_log),
        G([("debugger", "Debugger")], html_debugger), G([("memory", "Memory")], html_memory),
        G([("raw-state", "Raw state")], html_raw_state)])
    css = ("body{font-family:sans-serif;margin:1em 2em}table{border-collapse:collapse}td,th{border:1px solid #888;"
           "padding:2px 6px;text-align:left;font-family:monospace}pre{background:#f4f4f4;padding:6px;overflow:auto}"
           "img{image-rendering:pixelated;max-width:100%}figure{display:inline-block;margin:4px}")
    return ("<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><title>%s</title>"
            "<style>%s</style></head><body><h1>%s</h1>\n%s\n</body></html>\n" % (E(title), css, E(title), body))


# ---------------------------------------------------------------------------
# Text
# ---------------------------------------------------------------------------

def render_text(rep, with_memory):
    try:
        return sanitize(render_text_unsafe(rep, with_memory))
    except SHAPE_ERRORS:
        return sanitize("== Summary ==\n(the report holds data of an unexpected shape, not shown)\n")


def render_text_unsafe(rep, with_memory):
    m = rep.manifest
    p = m.get("producer", {}) if isinstance(m.get("producer"), dict) else {}
    out = ["== Summary ==",
           "jnext version: %s" % p.get("jnext_version", ""),
           "platform: %s  os: %s %s  arch: %s" % (p.get("platform", ""), p.get("os", ""), p.get("os_release", ""), p.get("arch", "")),
           "frontend: %s  toolkit: %s" % (p.get("frontend", ""), p.get("toolkit", "")),
           "machine: %s" % m.get("machine", ""), "created (UTC): %s" % m.get("created_utc", "")]
    for c in m.get("cards", []):
        if isinstance(c, dict):
            out.append("card %s: %s %s bytes sha256 %s" % (c.get("slot"), c.get("name"), c.get("bytes"), c.get("sha256")))
    for g in m.get("programs", []):
        if isinstance(g, dict):
            w = g.get("withheld") if isinstance(g.get("withheld"), dict) else {}
            out.append("program %s: %s bytes sha256 %s %s" % (g.get("name"), g.get("bytes"), g.get("sha256", ""),
                                                             "included" if g.get("included") else "withheld (%s)" % w.get("code", "")))
    ok, why = memory_state(rep)
    out.append("memory: %s" % ("included" if ok else "not included: " + why))
    for w in m.get("warnings", []):
        out.append("warning: %s" % w)
    mj = rep.json("machine.json")
    out += ["", "== Registers =="]
    if isinstance(mj, dict) and isinstance(mj.get("registers"), dict):
        for k, v in mj["registers"].items():
            v = int(v) if isinstance(v, bool) else v   # `halted` is a flag: 0 or 1
            out.append("%s = %s (%s)" % (k, hexn(v, reg_digits(k)), v))
    else:
        out.append("(machine.json not in this report)")
    if with_memory:
        out += ["", "== Memory (CPU view) =="]
        if ok:
            out.append(hex_dump_text(rep.raw("memory/cpu.bin")))
        else:
            out.append("not included: %s" % why)
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------

def main(argv):
    ap = argparse.ArgumentParser(prog="jnext-bug-view.py", description="Show a jnext bug report.")
    ap.add_argument("report")
    ap.add_argument("-o", dest="out", help="write the HTML page here instead of a temporary folder")
    ap.add_argument("--text", action="store_true", help="print plain text instead of an HTML page")
    ap.add_argument("--memory", action="store_true", help="with --text: add the hex dump of the CPU view")
    ap.add_argument("--no-open", action="store_true", help="do not open the page in the browser")
    args = ap.parse_args(argv)
    try:
        rep = Report(args.report)
        if args.text:
            sys.stdout.write(render_text(rep, args.memory))
            return 0
        page = render_html(rep)
        if args.out:
            path = args.out
        else:
            path = os.path.join(tempfile.mkdtemp(prefix="jnext-bug-view-"), "report.html")
        with open(path, "w", encoding="utf-8") as f:
            f.write(page)
        print(sanitize(path))
        if not args.no_open:
            webbrowser.open("file://" + os.path.abspath(path))
        return 0
    except Refused as e:
        sys.stderr.write(sanitize("jnext-bug-view: %s: %s\n" % (args.report, e)))
        return 2
    except (zipfile.BadZipFile, NotImplementedError, RuntimeError, EOFError, zlib.error) as e:
        # an unsupported or encrypted or corrupt member: a file this viewer will not show
        sys.stderr.write(sanitize("jnext-bug-view: %s: a member cannot be read (%s)\n" % (args.report, e)))
        return 2
    except OSError as e:
        sys.stderr.write(sanitize("jnext-bug-view: %s\n" % e))
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
