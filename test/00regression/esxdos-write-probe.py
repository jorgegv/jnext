#!/usr/bin/env python3
# The esxDOS write-call probe (GH #457): a NEX that runs a table of esxDOS calls
# that change files and prints one line per call through the magic port, and the
# tools that go with it. The program is IDENTICAL on every target: real NextZXOS
# booted in jnext (which RECORDS what NextZXOS answers, the oracle, see
# test/fixtures/esxdos-write-probe/README.md) and jnext's own host-folder back
# end (which is then judged against that recording).
#
#   esxdos-write-probe.py nex      --mode rw|ro OUT.nex   write the probe NEX
#   esxdos-write-probe.py annotate --mode rw|ro RAW.txt   print RAW with each step's description
#   esxdos-write-probe.py compare  --recorded FILE --mode rw|ro RAW.txt
#                                                         diff a run against the recording
#                                                         (exit 0: equal except declared divergences)
#   esxdos-write-probe.py steps    --mode rw|ro           list the step table
#
# Transcript line:  S<step hex4> F<carry> A<hex8 | **> [ extra ]
#   `A**` : the A register is a handle number (NextZXOS and jnext number handles
#   differently): masked when the call succeeded.
# Nothing in the transcript depends on a directory listing order or on a timestamp:
# state is read back by F_STAT of NAMED entries (file: attribute + size; a
# directory or a missing name: the error code).
#
# One generator, two modes. `rw` builds its own tree under C:/W457 and runs
# every call; it ends by leaving C:/W457/RO (a file, a read-only file, an empty
# directory, a non-empty directory). `ro` runs against that tree on a write-
# protected card (real) or a root without --esxdos-stub-writable (jnext); its
# F_STAT lines carry the size only (jnext marks every entry read-only on a
# read-only mount: documented, and not what is being measured here).
import sys, struct, hashlib

# ── mini assembler (two passes) ───────────────────────────────────────────────
CODE_ORG = 0x8000
MAGIC = 0xCAFE
CUR, REC, STEPNO, LASTFC, REGA = 0x8400, 0x8410, 0x8412, 0x8414, 0x8415
RBC, RDE, SAVEA, SLOTS = 0x8416, 0x8418, 0x841A, 0x8420
STAT, BUF, GERR, CWDB, PAT = 0x8500, 0x8600, 0x8700, 0x8720, 0x8800
TABLE_LO = 0x8A00          # table + strings go here, or in bank 0 at $C000 if they do not fit
BANK0 = 0xC000

items = []
labels = {}
def b(*v):    items.append(bytes(v))
def w16(l):   items.append(("w16", l))
def rel(l):   items.append(("rel", l))
def label(n): items.append(("label", n))
def jr(l):     b(0x18); rel(l)
def jr_nz(l):  b(0x20); rel(l)
def jr_z(l):   b(0x28); rel(l)
def jr_c(l):   b(0x38); rel(l)
def djnz(l):   b(0x10); rel(l)
def jp(l):     b(0xC3); w16(l)
def jp_z(l):   b(0xCA); w16(l)
def call(l):   b(0xCD); w16(l)
def lda_m(a):  b(0x3A, a & 255, a >> 8)
def sta_m(a):  b(0x32, a & 255, a >> 8)
def ldhl_m(a): b(0x2A, a & 255, a >> 8)
def sthl_m(a): b(0x22, a & 255, a >> 8)
def ldhl(v):   b(0x21, v & 255, v >> 8)
def ldde(v):   b(0x11, v & 255, v >> 8)
def ldbc(v):   b(0x01, v & 255, v >> 8)
def ldix_m(a): b(0xDD, 0x2A, a & 255, a >> 8)
def ldde_m(a): b(0xED, 0x5B, a & 255, a >> 8)
def ldbc_m(a): b(0xED, 0x4B, a & 255, a >> 8)
def stbc_m(a): b(0xED, 0x43, a & 255, a >> 8)
def stde_m(a): b(0xED, 0x53, a & 255, a >> 8)
def ld_a_n(n): b(0x3E, n)
def ld_b_n(n): b(0x06, n)
def and_n(n):  b(0xE6, n)
def cp_n(n):   b(0xFE, n)

def program():
    label("start")
    b(0xF3)                                   # di
    items.append(("tablehl",))                # ld hl,TABLE
    sthl_m(REC)
    ldhl(0); sthl_m(STEPNO)
    label("next")
    ldhl_m(REC)
    b(0x7E); b(0xB7); jp_z("finish")          # ld a,(hl) ; or a ; jp z
    ldde(CUR); ldbc(14); b(0xED, 0xB0)        # ldir
    sthl_m(REC)
    lda_m(CUR); b(0x32); w16("hookp")         # patch the hook byte
    lda_m(CUR + 2); and_n(1); lda_m(CUR + 1); jr_z("havea")
    b(0x5F); b(0x16, 0)                       # ld e,a ; ld d,0
    ldhl(SLOTS); b(0x19); b(0x7E)             # add hl,de ; ld a,(hl)
    label("havea")
    sta_m(SAVEA)
    ldix_m(CUR + 4); ldde_m(CUR + 6); ldbc_m(CUR + 8)
    lda_m(SAVEA)
    b(0xCF)                                   # rst 8
    label("hookp"); b(0)
    stbc_m(RBC); stde_m(RDE)
    b(0xF5); b(0xC1)                          # push af ; pop bc   (B=A, C=F)
    b(0x79); and_n(1); sta_m(LASTFC)          # ld a,c
    b(0x78); sta_m(REGA)                      # ld a,b
    lda_m(CUR + 2); and_n(2); jr_z("nostore")
    lda_m(LASTFC); b(0xB7); jr_nz("nostore")
    lda_m(CUR + 3); b(0x5F); b(0x16, 0); ldhl(SLOTS); b(0x19)
    lda_m(REGA); b(0x77)                      # ld (hl),a
    label("nostore")
    call("print_line"); call("do_extra")
    ld_a_n(10); call("out_a")
    lda_m(CUR + 2); and_n(8); jr_z("noclose")
    lda_m(LASTFC); b(0xB7); jr_nz("noclose")
    lda_m(REGA); b(0xCF, 0x9B)                # F_CLOSE the handle just opened
    label("noclose")
    ldhl_m(STEPNO); b(0x23); sthl_m(STEPNO)
    jp("next")
    label("finish")
    for ch in "END\n":
        ld_a_n(ord(ch)); call("out_a")
    label("spin"); jr("spin")

    label("out_a")                            # A -> magic port; keeps BC, HL
    b(0xC5); ldbc(MAGIC); b(0xED, 0x79); b(0xC1); b(0xC9)
    label("hex_nib")
    and_n(0x0F); b(0xC6, 0x30); cp_n(0x3A); jr_c("hn_ok"); b(0xC6, 0x07)
    label("hn_ok"); jp("out_a")
    label("hex8")
    b(0xF5); b(0x0F); b(0x0F); b(0x0F); b(0x0F); call("hex_nib"); b(0xF1); jp("hex_nib")
    label("hex16")
    b(0x7C); call("hex8"); b(0x7D); jp("hex8")
    label("print_line")
    for ch in "S":
        ld_a_n(ord(ch)); call("out_a")
    ldhl_m(STEPNO); call("hex16")
    ld_a_n(32); call("out_a"); ld_a_n(ord('F')); call("out_a")
    lda_m(LASTFC); b(0xC6, 0x30); call("out_a")
    ld_a_n(32); call("out_a"); ld_a_n(ord('A')); call("out_a")
    lda_m(CUR + 2); and_n(4); jr_z("pa_real")
    lda_m(LASTFC); b(0xB7); jr_nz("pa_real")
    ld_a_n(ord('*')); call("out_a"); ld_a_n(ord('*')); jp("out_a")
    label("pa_real"); lda_m(REGA); jp("hex8")
    label("do_extra")
    lda_m(CUR + 10); b(0xB7); b(0xC8)         # ret z
    cp_n(6); jp_z("x_geterr")
    b(0x47); lda_m(LASTFC); b(0xB7); b(0xC0); b(0x78)   # ld b,a; ...; ret nz; ld a,b
    cp_n(1); jr_z("x_bc"); cp_n(2); jr_z("x_bcde"); cp_n(3); jr_z("x_s3")
    cp_n(4); jr_z("x_s4"); cp_n(5); jr_z("x_mem"); cp_n(7); jr_z("x_str"); cp_n(8); jr_z("x_rd"); b(0xC9)
    label("x_bc")
    ld_a_n(32); call("out_a"); ldhl_m(RBC); jp("hex16")
    label("x_bcde")
    call("x_bc"); ldhl_m(RDE); jp("hex16")
    label("x_s3")
    ld_a_n(32); call("out_a"); lda_m(STAT + 2); call("hex8")
    label("x_s4")
    ldhl(STAT + 7); ld_b_n(4); jp("dump_mem")
    label("x_mem")
    ldhl_m(CUR + 12); lda_m(CUR + 11); b(0x47); jp("dump_mem")
    label("x_rd")                             # F_READDIR entry (A=1): attr, then the name
    lda_m(REGA); b(0xB7); b(0xC8)
    ldhl_m(CUR + 12); ld_a_n(32); call("out_a"); b(0x7E); b(0xE5); call("hex8"); b(0xE1); b(0x23)
    ld_a_n(32); call("out_a"); jr("x_str_go")
    label("x_str")
    ld_a_n(32); call("out_a"); ldhl_m(CUR + 12)
    label("x_str_go")
    ld_b_n(64)
    label("xs_l")
    b(0x7E); b(0xB7); b(0xC8); b(0xE5); b(0xC5); call("out_a"); b(0xC1); b(0xE1); b(0x23); djnz("xs_l"); b(0xC9)
    label("x_geterr")
    ld_a_n(32); call("out_a"); ldhl_m(CUR + 12); ld_b_n(32)
    label("xg_l")
    b(0x7E); b(0xE5); b(0xC5); b(0xF5); call("hex8"); b(0xF1); b(0xC1); b(0xE1)
    b(0xCB, 0x7F); b(0xC0)                    # bit 7,a ; ret nz
    b(0x23); djnz("xg_l"); b(0xC9)
    label("dump_mem")                         # HL=addr, B=len (>0)
    ld_a_n(32); call("out_a")
    label("dm_l")
    b(0x7E); b(0xE5); b(0xC5); call("hex8"); b(0xC1); b(0xE1); b(0x23); djnz("dm_l"); b(0xC9)

# ── the step table ──────────────────────────────────────────────────────────
STAR = ord('*')
H = dict(M_DOSVERSION=0x88, M_GETERR=0x93, F_OPEN=0x9A, F_CLOSE=0x9B, F_READ=0x9D,
         F_WRITE=0x9E, F_SEEK=0x9F, F_FGETPOS=0xA0, F_FSTAT=0xA1, F_FTRUNCATE=0xA2,
         F_OPENDIR=0xA3, F_READDIR=0xA4, F_GETCWD=0xA8, F_CHDIR=0xA9, F_MKDIR=0xAA, F_RMDIR=0xAB,
         F_STAT=0xAC, F_UNLINK=0xAD, F_TRUNCATE=0xAE, F_CHMOD=0xAF, F_RENAME=0xB0)
NAME = {v: k for k, v in H.items()}

class Step:
    pass

class Gen:
    def __init__(self, mode):
        self.mode = mode
        self.steps = []
        self.group = ""
        self.cur_tag = None      # a declared divergence covering the steps made while it is set

    # one raw step
    def T(self, desc, hook, a=STAR, ix=0, de=0, bc=0, kind=0, ln=0, addr=0,
          slot=None, store=None, mask=None, close=False, hdr=False, tag=None):
        s = Step()
        s.desc = "[%s] %s" % (self.group, desc) if self.group else desc
        s.hook = H[hook] if isinstance(hook, str) else hook
        s.a = a; s.ix = ix; s.de = de; s.bc = bc
        s.kind = kind; s.ln = ln; s.addr = addr
        s.slot = slot; s.store = store; s.close = close
        # A on success is whatever the register held (NextZXOS leaves junk there, measured):
        # masked, except for the calls whose A is an answer
        s.mask = mask if mask is not None else (hook not in ("M_GETERR", "F_READDIR"))
        s.hdr = hdr; s.tag = tag or self.cur_tag
        self.steps.append(s)
        return s

    def stat(self, path, why=""):
        k = 3 if self.mode == "rw" else 4
        self.T("F_STAT %s %s" % (path, why), "F_STAT", ix=path, de=STAT, kind=k)

    def exists(self, path):          # existence / error code only
        self.T("F_STAT %s (exists?)" % path, "F_STAT", ix=path, de=STAT)

    def mkfile(self, path, size, why=""):
        self.T("create %s (%d bytes) %s" % (path, size, why), "F_OPEN", ix=path,
               bc=(0x0E << 8), store=7, mask=True)
        if size:
            self.T("write %d bytes" % size, "F_WRITE", slot=7, ix=PAT, bc=size, kind=1)
        self.T("close", "F_CLOSE", slot=7)

    def mkdir(self, path, why=""):
        self.T("F_MKDIR %s %s" % (path, why), "F_MKDIR", ix=path)

    def chmod(self, path, values, mask, why=""):
        self.T("F_CHMOD %s B=%02X C=%02X %s" % (path, values, mask, why), "F_CHMOD",
               ix=path, bc=(values << 8) | mask)

    def ro(self, path):              # make read-only: clear A_WRITE
        self.chmod(path, 0x00, 0x01, "(read-only)")

    def cwd(self):
        self.T("F_GETCWD", "F_GETCWD", ix=CWDB, kind=7, addr=CWDB)

    def open_probe(self, path, mode, why=""):
        self.T("F_OPEN %s mode %02X %s" % (path, mode, why), "F_OPEN", ix=path,
               bc=mode << 8, mask=True, close=True)

W = "C:/W457"

def build(mode):
    g = Gen(mode)
    T = g.T
    ro_mode = (mode == "ro")
    T("M_DOSVERSION (header, not compared)", "M_DOSVERSION", kind=2, hdr=True)
    if ro_mode:
        build_ro(g)
    else:
        build_rw(g)
    return g

def chmod_unserved(v, m):
    """True for a F_CHMOD request jnext refuses with esx_enosys (it has no host form)."""
    return bool(m & ~0xA1) or bool((m & 0x20) and not (v & 0x20))

def build_rw(g):
    T = g.T
    T("F_CHDIR C:/", "F_CHDIR", ix="C:/")
    g.mkdir(W)
    T("F_CHDIR " + W, "F_CHDIR", ix=W)
    g.cwd()

    # ── containment: '..' above the root ────────────────────────────────────
    g.group = "dotdot"
    g.mkfile(W + "/a.txt", 17)
    esc = ["C:/..", "C:/../W457/a.txt", W + "/../../W457/a.txt", "C:/../esc.txt"]
    for p in esc:
        g.T("F_STAT %s" % p, "F_STAT", ix=p, de=STAT, kind=3,
            tag="bare-dotdot-stat" if p == "C:/.." else None)
    for p in esc[1:]:
        g.open_probe(p, 0x01, "read")
    g.open_probe("C:/../esc.txt", 0x0E, "create")
    g.exists("C:/esc.txt")
    g.T("F_OPENDIR C:/..", "F_OPENDIR", ix="C:/..", bc=0x1000, mask=True, close=True)
    g.T("F_CHDIR C:/..", "F_CHDIR", ix="C:/..")
    g.cwd()
    g.T("F_CHDIR " + W, "F_CHDIR", ix=W)
    g.mkdir("C:/../escdir", "(above the root)")
    g.exists("C:/escdir")
    g.T("F_RMDIR C:/..", "F_RMDIR", ix="C:/..")
    g.T("F_TRUNCATE C:/../W457/a.txt 3", "F_TRUNCATE", ix="C:/../W457/a.txt", bc=0, de=3)
    g.chmod("C:/../W457/a.txt", 0x00, 0x01)
    g.T("F_RENAME src above root", "F_RENAME", ix="C:/../W457/a.txt", de=W + "/b.txt")
    g.T("F_RENAME dst above root", "F_RENAME", ix=W + "/a.txt", de="C:/../esc2.txt")
    g.T("F_UNLINK C:/..", "F_UNLINK", ix="C:/..")
    g.T("F_UNLINK C:/../W457/a.txt", "F_UNLINK", ix="C:/../W457/a.txt")
    g.stat(W + "/a.txt", "(after the escapes)")
    g.exists(W + "/b.txt"); g.exists("C:/esc2.txt")
    # the root's own '..' relative: from a subdirectory
    g.mkdir(W + "/sub")
    g.T("F_CHDIR W457/sub", "F_CHDIR", ix=W + "/sub")
    g.T("F_STAT ../../../W457/a.txt (relative above root)", "F_STAT", ix="../../../W457/a.txt", de=STAT, kind=3)
    g.T("F_STAT ../../a.txt", "F_STAT", ix="../../a.txt", de=STAT, kind=3)
    g.T("F_CHDIR " + W, "F_CHDIR", ix=W)
    # a bare '..' above the root, absolute, for every call
    g.T("F_OPEN C:/.. read", "F_OPEN", ix="C:/..", bc=0x0100, mask=True, close=True)
    g.T("F_OPEN C:/.. create", "F_OPEN", ix="C:/..", bc=0x0E00, mask=True, close=True)
    g.mkdir("C:/..", "(bare, above the root)")
    g.T("F_TRUNCATE C:/..", "F_TRUNCATE", ix="C:/..", bc=0, de=3)
    g.chmod("C:/..", 0x00, 0x01, "(bare, above the root)")
    g.T("F_RENAME C:/.. -> x", "F_RENAME", ix="C:/..", de=W + "/bx")
    g.T("F_RENAME a.txt -> C:/..", "F_RENAME", ix=W + "/a.txt", de="C:/..")
    g.exists(W + "/a.txt")
    g.T("F_UNLINK C:/../*.txt", "F_UNLINK", ix="C:/../*.txt")
    g.T("F_RMDIR C:/../x", "F_RMDIR", ix="C:/../x")
    g.T("F_CHDIR C:/../..", "F_CHDIR", ix="C:/../..")
    g.T("F_STAT C:/../..", "F_STAT", ix="C:/../..", de=STAT, kind=3)
    # the same, relative, with the root as the CWD
    g.T("F_CHDIR C:/", "F_CHDIR", ix="C:/")
    for nm, p in [("..", ".."), ("../x", "../x"), ("../..", "../.."), ("W457/../..", "W457/../..")]:
        g.T("F_STAT %s from the root" % nm, "F_STAT", ix=p, de=STAT, kind=3,
            tag="bare-dotdot-stat" if nm in ("..", "W457/../..") else None)
        g.T("F_OPENDIR %s from the root" % nm, "F_OPENDIR", ix=p, bc=0x1000, mask=True, close=True)
        g.T("F_CHDIR %s from the root" % nm, "F_CHDIR", ix=p)
        g.T("F_OPEN %s from the root, create" % nm, "F_OPEN", ix=p, bc=0x0E00, mask=True, close=True)
        g.T("F_UNLINK %s from the root" % nm, "F_UNLINK", ix=p)
        g.T("F_RMDIR %s from the root" % nm, "F_RMDIR", ix=p)
    g.cwd()
    g.T("F_CHDIR " + W, "F_CHDIR", ix=W)

    # ── F_UNLINK basics ─────────────────────────────────────────────────────
    g.group = "unlink"
    g.mkdir(W + "/u")
    U = W + "/u/"
    g.mkfile(U + "f1.txt", 31); g.mkfile(U + "f2.txt", 32); g.mkfile(U + "Long Name.txt", 33)
    g.mkdir(U + "d"); g.mkdir(U + "nd"); g.mkfile(U + "nd/in.txt", 34)
    g.mkfile(U + "ro.txt", 35); g.ro(U + "ro.txt")
    g.mkfile(U + "open.txt", 36)
    T = g.T
    T("F_UNLINK f1.txt", "F_UNLINK", ix=U + "f1.txt")
    g.exists(U + "f1.txt"); g.stat(U + "f2.txt", "(neighbour kept)")
    T("F_UNLINK f1.txt again (missing)", "F_UNLINK", ix=U + "f1.txt")
    T("F_UNLINK missing dir", "F_UNLINK", ix=U + "nodir/x.txt")
    T("F_UNLINK empty string", "F_UNLINK", ix="")
    T("F_UNLINK C:/", "F_UNLINK", ix="C:/")
    T("F_UNLINK empty directory d", "F_UNLINK", ix=U + "d")
    g.exists(U + "d")
    T("F_UNLINK non-empty directory nd", "F_UNLINK", ix=U + "nd")
    g.exists(U + "nd"); g.exists(U + "nd/in.txt")
    T("F_UNLINK read-only file", "F_UNLINK", ix=U + "ro.txt")
    g.exists(U + "ro.txt")
    T("open for write", "F_OPEN", ix=U + "open.txt", bc=(0x02 << 8), store=0, mask=True)
    T("F_UNLINK open file", "F_UNLINK", ix=U + "open.txt")
    g.exists(U + "open.txt")
    T("close", "F_CLOSE", slot=0)
    T("F_UNLINK it after close", "F_UNLINK", ix=U + "open.txt")
    g.exists(U + "open.txt")
    T("open for read", "F_OPEN", ix=U + "f2.txt", bc=(0x01 << 8), store=0, mask=True)
    T("F_UNLINK file open for read", "F_UNLINK", ix=U + "f2.txt")
    T("close", "F_CLOSE", slot=0)
    g.exists(U + "f2.txt")
    T("F_UNLINK 8.3 name LONGNA~1.TXT", "F_UNLINK", ix=U + "LONGNA~1.TXT")
    g.exists(U + "Long Name.txt")
    g.mkfile(U + "Case.TXT", 37)
    T("F_UNLINK case variant case.txt", "F_UNLINK", ix=U + "case.txt")
    g.exists(U + "Case.TXT"); g.exists(U + "CASE.TXT")
    T("F_UNLINK relative from the CWD (W457): u/Case.TXT", "F_UNLINK", ix="u/Case.TXT")
    g.exists(U + "Case.TXT")

    # ── wildcard matcher, one fresh directory per pattern ───────────────────
    g.group = "wild"
    files = [("Long Name.txt", 41), ("longna.txt", 42), ("a.txt", 43), ("ab.txt", 44),
             ("abc.txt", 45), ("x.tar.gz", 46), ("a.tx", 47), ("b", 48)]
    pats = ["L*.txt", "*.TXT", "*.t*", "Long?Name.txt", "*NAME.TXT", "*ame.txt", "L*e.txt",
            "*~1.txt", "Long*", "Lo*", "*.gz", "*.tx", "a?.txt", "b.*", "l*.TXT",
            "LONGNA~1.*", "??????~1.TXT", "*", "*.none", "*.*"]
    for i, pat in enumerate(pats):
        d = "%s/w%02d" % (W, i)
        g.mkdir(d)
        for n, sz in files:
            g.T("create %s/%s" % (d, n), "F_OPEN", ix="%s/%s" % (d, n), bc=(0x0E << 8), mask=True, close=True)
        g.T("F_UNLINK %s/%s" % (d, pat), "F_UNLINK", ix="%s/%s" % (d, pat))
        for n, sz in files:
            g.T("exists %s after pattern %s" % (n, pat), "F_STAT", ix="%s/%s" % (d, n), de=STAT)
    # wildcard unlink: skips directories; stops at a refused (read-only) match
    g.group = "wildmix"
    d = W + "/wm"
    g.mkdir(d)
    g.mkdir(d + "/d.txt")
    for n in ["a.txt", "b.txt", "c.txt", "e.txt"]:
        g.mkfile("%s/%s" % (d, n), 50)
    g.ro(d + "/c.txt")
    T("F_UNLINK *.txt (dir d.txt matches; c.txt is read-only)", "F_UNLINK", ix=d + "/*.txt")
    for n in ["d.txt", "a.txt", "b.txt", "c.txt", "e.txt"]:
        g.exists("%s/%s" % (d, n))
    T("F_UNLINK *.txt again (c.txt stops it)", "F_UNLINK", ix=d + "/*.txt")
    for n in ["a.txt", "b.txt", "c.txt", "e.txt"]:
        g.exists("%s/%s" % (d, n))
    g.mkdir(d + "/sub")
    T("F_UNLINK * in an empty subdirectory", "F_UNLINK", ix=d + "/sub/*")
    g.exists(d + "/sub")
    T("F_UNLINK wildcard in a directory component", "F_UNLINK", ix=d + "/s*/x.txt")
    T("F_UNLINK no match", "F_UNLINK", ix=d + "/*.zzz")

    # ── F_RENAME ─────────────────────────────────────────────────────────────
    g.group = "rename"
    d = W + "/r"
    g.mkdir(d)
    g.mkfile(d + "/a.txt", 61); g.mkfile(d + "/b.txt", 62); g.mkfile(d + "/o.txt", 63)
    g.mkfile(d + "/Case.txt", 64)
    g.mkdir(d + "/dir"); g.mkdir(d + "/dir2"); g.mkdir(d + "/dir2/dir")
    g.mkfile(d + "/dir/in.txt", 65); g.mkfile(d + "/ro.txt", 66); g.ro(d + "/ro.txt")
    def rn(desc, s, t): g.T("F_RENAME %s -> %s (%s)" % (s, t, desc), "F_RENAME", ix=s, de=t)
    rn("in place", d + "/a.txt", d + "/a2.txt")
    g.stat(d + "/a2.txt"); g.exists(d + "/a.txt")
    rn("onto an existing file", d + "/a2.txt", d + "/b.txt")
    g.stat(d + "/a2.txt"); g.stat(d + "/b.txt")
    rn("to a case variant of itself", d + "/Case.txt", d + "/case.txt")
    g.exists(d + "/Case.txt"); g.exists(d + "/case.txt")
    rn("onto a case variant of another file", d + "/a2.txt", d + "/B.TXT")
    rn("into an existing directory", d + "/a2.txt", d + "/dir")
    g.exists(d + "/dir/a2.txt"); g.exists(d + "/a2.txt")
    g.mkfile(d + "/dir2/dir/a2.txt", 67)
    rn("into a directory holding the name", d + "/dir/a2.txt", d + "/dir2/dir")
    g.stat(d + "/dir/a2.txt"); g.stat(d + "/dir2/dir/a2.txt")
    rn("a directory", d + "/dir2/dir", d + "/dir3")
    g.exists(d + "/dir3/a2.txt"); g.exists(d + "/dir2/dir")
    rn("a directory into its own subtree", d + "/dir2", d + "/dir2/dir3")
    g.exists(d + "/dir2")
    g.T("open for write", "F_OPEN", ix=d + "/o.txt", bc=(0x02 << 8), store=0, mask=True)
    rn("an open file", d + "/o.txt", d + "/o2.txt")
    g.T("close", "F_CLOSE", slot=0)
    g.exists(d + "/o.txt"); g.exists(d + "/o2.txt")
    rn("a read-only file", d + "/ro.txt", d + "/ro2.txt")
    g.stat(d + "/ro2.txt", "(still read-only?)")
    rn("wildcard source", d + "/*.txt", d + "/zz.txt")
    rn("wildcard destination", d + "/o.txt", d + "/z*.txt")
    rn("missing source", d + "/none.txt", d + "/n2.txt")
    rn("into a missing directory", d + "/o.txt", d + "/nodir/o.txt")
    rn("source '.'", d + "/.", d + "/dotx")
    rn("source '..'", d + "/..", d + "/dotdotx")
    rn("source the root", "C:/", d + "/rootx")
    rn("an empty destination", d + "/o.txt", "")
    g.mkfile(d + "/toroot.txt", 72)
    rn("to the root (into it)", d + "/toroot.txt", "C:/")
    g.exists(d + "/toroot.txt"); g.stat("C:/toroot.txt")
    g.mkdir(d + "/cd"); g.mkfile(d + "/cd/f.txt", 68); g.mkfile(d + "/cd/g.txt", 69)
    g.T("F_CHDIR r/cd", "F_CHDIR", ix=d + "/cd")
    g.T("F_RENAME f.txt -> ../moved.txt (relative dest from the CWD)", "F_RENAME", ix="f.txt", de="../moved.txt")
    g.exists(d + "/moved.txt"); g.exists(d + "/cd/f.txt"); g.exists(d + "/cd/../moved.txt")
    g.T("F_CHDIR " + W, "F_CHDIR", ix=W)
    # a directory holding an open file, then read through the handle
    g.mkdir(d + "/hd"); g.mkfile(d + "/hd/h.txt", 70)
    g.T("open hd/h.txt read", "F_OPEN", ix=d + "/hd/h.txt", bc=(0x01 << 8), store=1, mask=True)
    g.T("seek 5", "F_SEEK", slot=1, bc=0, de=5, kind=2)
    rn("a directory holding an open file", d + "/hd", d + "/hd2")
    g.T("F_FGETPOS through the handle", "F_FGETPOS", slot=1, kind=2)
    g.T("F_READ 4 bytes through the handle", "F_READ", slot=1, ix=BUF, bc=4, kind=5, ln=4, addr=BUF)
    g.T("F_FGETPOS", "F_FGETPOS", slot=1, kind=2)
    g.T("close", "F_CLOSE", slot=1)
    g.exists(d + "/hd2/h.txt")
    # renaming the CWD
    g.mkdir(d + "/cwd")
    g.T("F_CHDIR r/cwd", "F_CHDIR", ix=d + "/cwd")
    rn("the CWD", d + "/cwd", d + "/cwd2")
    g.cur_tag = "cwd-follows-rename"
    g.cwd()
    g.cur_tag = None
    g.mkfile("made.txt", 71, "(relative create after renaming the CWD)")
    g.exists(d + "/cwd2/made.txt"); g.exists(d + "/cwd/made.txt")
    g.T("F_CHDIR " + W, "F_CHDIR", ix=W)

    # ── F_MKDIR ──────────────────────────────────────────────────────────────
    g.group = "mkdir"
    d = W + "/m"
    g.mkdir(d)
    g.mkdir(d + "/new"); g.exists(d + "/new")
    g.mkdir(d + "/new", "(existing dir)")
    g.mkfile(d + "/file.txt", 81)
    g.mkdir(d + "/file.txt", "(existing file)")
    g.mkdir("C:/", "(the root)")
    g.mkdir(d + "/no/parent", "(missing parent)")
    g.exists(d + "/no")
    g.mkdir(d + "/w*", "(wildcard)")
    g.mkdir(d + "/w?")
    g.mkdir(d + "/NEW", "(case variant of existing)")
    g.mkdir(d + "/Long Dir Name")
    g.exists(d + "/LONGDI~1")
    g.mkdir("")
    for ch in ['"', "<", ">", "|", "*", "?", ":"]:
        name = d + "/x" + ch + "y"
        g.mkdir(name, "(FAT-forbidden character)")
        g.exists(name)
        g.T("create file with it", "F_OPEN", ix=d + "/f" + ch + "g.txt", bc=(0x0E << 8), mask=True, close=True)
        g.exists(d + "/f" + ch + "g.txt")
    g.mkfile(d + "/rn.txt", 82)
    g.T("F_RENAME to a name with a quote", "F_RENAME", ix=d + "/rn.txt", de=d + '/r"n.txt')
    g.exists(d + "/rn.txt"); g.exists(d + '/r"n.txt')
    g.T("F_RENAME to a name with <", "F_RENAME", ix=d + "/rn.txt", de=d + "/r<n.txt")
    g.T("F_RENAME to a name with |", "F_RENAME", ix=d + "/rn.txt", de=d + "/r|n.txt")
    g.T("F_RENAME to a name with >", "F_RENAME", ix=d + "/rn.txt", de=d + "/r>n.txt")
    g.exists(d + "/rn.txt")
    g.mkdir(d + "/dot.", "(trailing dot)")
    g.exists(d + "/dot"); g.exists(d + "/dot.")
    g.T("F_OPENDIR dot.", "F_OPENDIR", ix=d + "/dot.", bc=0x1000, mask=True, close=True)
    g.T("F_OPENDIR dot", "F_OPENDIR", ix=d + "/dot", bc=0x1000, mask=True, close=True)
    g.cur_tag = "trailing-space"
    g.mkdir(d + "/sp ", "(trailing space)")
    g.exists(d + "/sp"); g.exists(d + "/sp ")
    g.T("F_OPENDIR 'sp '", "F_OPENDIR", ix=d + "/sp ", bc=0x1000, mask=True, close=True)
    g.T("F_OPENDIR sp", "F_OPENDIR", ix=d + "/sp", bc=0x1000, mask=True, close=True)
    g.cur_tag = None
    g.T("F_CHDIR into new", "F_CHDIR", ix=d + "/new"); g.cwd()
    g.T("F_CHDIR " + W, "F_CHDIR", ix=W)

    # ── F_RMDIR ──────────────────────────────────────────────────────────────
    g.group = "rmdir"
    d = W + "/rd"
    g.mkdir(d)
    for n in ["e", "ne", "e2", "cw", "od"]:
        g.mkdir(d + "/" + n)
    g.mkfile(d + "/ne/f.txt", 91); g.mkfile(d + "/file.txt", 92)
    T("F_RMDIR empty", "F_RMDIR", ix=d + "/e"); g.exists(d + "/e")
    T("F_RMDIR non-empty", "F_RMDIR", ix=d + "/ne"); g.exists(d + "/ne/f.txt")
    T("F_RMDIR a file", "F_RMDIR", ix=d + "/file.txt"); g.exists(d + "/file.txt")
    T("F_RMDIR missing", "F_RMDIR", ix=d + "/none")
    T("F_RMDIR the root", "F_RMDIR", ix="C:/")
    T("F_RMDIR empty string", "F_RMDIR", ix="")
    T("F_RMDIR wildcard", "F_RMDIR", ix=d + "/e*"); g.exists(d + "/e2")
    T("F_RMDIR dot", "F_RMDIR", ix=d + "/.")
    T("F_CHDIR rd/cw", "F_CHDIR", ix=d + "/cw")
    T("F_RMDIR the CWD", "F_RMDIR", ix=d + "/cw"); g.exists(d + "/cw")
    T("F_RMDIR '.' from inside", "F_RMDIR", ix=".")
    T("F_RMDIR a parent of the CWD", "F_RMDIR", ix=d)
    T("F_CHDIR " + W, "F_CHDIR", ix=W)
    T("F_OPENDIR rd/od", "F_OPENDIR", ix=d + "/od", bc=0x1000, store=2, mask=True)
    T("F_RMDIR a directory with an open F_OPENDIR handle", "F_RMDIR", ix=d + "/od")
    g.exists(d + "/od")
    T("close", "F_CLOSE", slot=2)
    T("F_RMDIR it after close", "F_RMDIR", ix=d + "/od"); g.exists(d + "/od")

    # ── F_TRUNCATE / F_FTRUNCATE ─────────────────────────────────────────────
    g.group = "trunc"
    d = W + "/t"
    g.mkdir(d)
    g.mkfile(d + "/s.txt", 120)
    def tr(path, size, why=""):
        g.T("F_TRUNCATE %s to %d %s" % (path, size, why), "F_TRUNCATE", ix=path, bc=size >> 16, de=size & 0xFFFF)
    tr(d + "/s.txt", 50, "(shrink)"); g.stat(d + "/s.txt")
    tr(d + "/s.txt", 50, "(same size)"); g.stat(d + "/s.txt")
    tr(d + "/s.txt", 0, "(to 0)"); g.stat(d + "/s.txt")
    tr(d + "/s.txt", 200, "(extend)"); g.stat(d + "/s.txt")
    g.T("open to read the extension", "F_OPEN", ix=d + "/s.txt", bc=(0x01 << 8), store=0, mask=True)
    g.T("seek 190", "F_SEEK", slot=0, bc=0, de=190, kind=2)
    g.T("read 10 (zero fill?)", "F_READ", slot=0, ix=BUF, bc=10, kind=5, ln=10, addr=BUF)
    g.T("close", "F_CLOSE", slot=0)
    tr(d + "/s.txt", 0x00012345, "(BCDE = 00012345, 74565)"); g.stat(d + "/s.txt")
    g.mkdir(d + "/dd")
    tr(d + "/dd", 5, "(a directory)"); g.exists(d + "/dd")
    tr(d + "/none", 5, "(missing)")
    tr(d + "/*.txt", 5, "(wildcard)"); g.stat(d + "/s.txt")
    g.mkdir(d + "/tw")
    g.mkfile(d + "/tw/t1.txt", 10); g.mkfile(d + "/tw/t2.txt", 20); g.mkfile(d + "/tw/t3.dat", 30)
    tr(d + "/tw/*.txt", 3, "(wildcard, two matches)")
    g.stat(d + "/tw/t1.txt"); g.stat(d + "/tw/t2.txt"); g.stat(d + "/tw/t3.dat")
    tr(d + "/tw/*.zzz", 3, "(wildcard, no match)")
    g.mkfile(d + "/o.txt", 100)
    g.T("open o.txt write", "F_OPEN", ix=d + "/o.txt", bc=(0x02 << 8), store=0, mask=True)
    tr(d + "/o.txt", 10, "(an open file)"); g.stat(d + "/o.txt")
    g.T("close", "F_CLOSE", slot=0)
    g.mkfile(d + "/r.txt", 100); g.ro(d + "/r.txt")
    tr(d + "/r.txt", 10, "(read-only file)"); g.stat(d + "/r.txt")
    # F_FTRUNCATE
    g.mkfile(d + "/f.txt", 100)
    g.T("open f.txt read+write (mode 03)", "F_OPEN", ix=d + "/f.txt", bc=(0x03 << 8), store=0, mask=True)
    g.T("seek 60", "F_SEEK", slot=0, bc=0, de=60, kind=2)
    g.T("F_FTRUNCATE to 30 (shrink below the position)", "F_FTRUNCATE", slot=0, bc=0, de=30)
    g.T("F_FGETPOS", "F_FGETPOS", slot=0, kind=2)
    g.T("F_FSTAT", "F_FSTAT", slot=0, ix=STAT, kind=3)
    g.T("F_FTRUNCATE to 90 (extend)", "F_FTRUNCATE", slot=0, bc=0, de=90)
    g.T("F_FSTAT", "F_FSTAT", slot=0, ix=STAT, kind=3)
    g.T("F_FGETPOS", "F_FGETPOS", slot=0, kind=2)
    g.T("seek 80", "F_SEEK", slot=0, bc=0, de=80, kind=2)
    g.T("read 10 (zero fill?)", "F_READ", slot=0, ix=BUF, bc=10, kind=5, ln=10, addr=BUF)
    g.T("close", "F_CLOSE", slot=0)
    g.stat(d + "/f.txt")
    g.T("open f.txt read (mode 01)", "F_OPEN", ix=d + "/f.txt", bc=(0x01 << 8), store=0, mask=True)
    g.T("F_FTRUNCATE a read handle", "F_FTRUNCATE", slot=0, bc=0, de=10)
    g.T("close", "F_CLOSE", slot=0)
    g.T("F_FTRUNCATE the closed handle", "F_FTRUNCATE", slot=0, bc=0, de=10)
    g.stat(d + "/f.txt")

    # ── F_CHMOD ──────────────────────────────────────────────────────────────
    g.group = "chmod"
    d = W + "/c"
    g.mkdir(d)
    g.mkfile(d + "/f.txt", 101); g.mkdir(d + "/dd")
    for nm, v, m in [("A_WRITE clear", 0x00, 0x01), ("A_WRITE set", 0x01, 0x01),
                     ("A_READ clear", 0x00, 0x80), ("A_READ set", 0x80, 0x80),
                     ("A_HIDDEN set", 0x02, 0x02), ("A_HIDDEN clear", 0x00, 0x02),
                     ("A_SYSTEM set", 0x04, 0x04), ("A_SYSTEM clear", 0x00, 0x04),
                     ("A_ARCH clear", 0x00, 0x20), ("A_ARCH set", 0x20, 0x20),
                     ("mask 0", 0x01, 0x00), ("all bits clear $FF", 0x00, 0xFF),
                     ("all bits set $FF", 0xFF, 0xFF), ("mask $26 clear", 0x00, 0x26),
                     ("mask $26 set", 0x26, 0x26)]:
        g.cur_tag = "chmod-unserved" if chmod_unserved(v, m) else None
        g.chmod(d + "/f.txt", v, m, "(" + nm + ")")
        g.stat(d + "/f.txt")
        g.cur_tag = None
    g.chmod(d + "/f.txt", 0x01, 0x01); g.chmod(d + "/f.txt", 0x20, 0x20)
    for nm, v, m in [("write+read clear", 0x00, 0x81), ("write+read set", 0x81, 0x81),
                     ("write set, read clear", 0x01, 0x81), ("write clear, read set", 0x80, 0x81),
                     ("write+arch clear", 0x00, 0x21), ("write+arch set", 0x21, 0x21),
                     ("write set, arch clear", 0x01, 0x21), ("write clear, arch set", 0x20, 0x21)]:
        g.cur_tag = "chmod-unserved" if chmod_unserved(v, m) else None
        g.chmod(d + "/f.txt", v, m, "(" + nm + ")")
        g.stat(d + "/f.txt")
        g.cur_tag = None
        g.chmod(d + "/f.txt", 0x01, 0x01); g.chmod(d + "/f.txt", 0x20, 0x20)
    g.mkdir(d + "/pd"); g.mkdir(d + "/pd/dd")
    def lsdir(why):
        g.T("F_OPENDIR c/pd " + why, "F_OPENDIR", ix=d + "/pd", bc=0x1000, store=2)
        for i in range(4):
            g.T("F_READDIR entry %d" % i, "F_READDIR", slot=2, ix=BUF, de=0, kind=8, addr=BUF)
        g.T("close", "F_CLOSE", slot=2)
    lsdir("(before)")
    g.cur_tag = "chmod-dir"
    for nm, v, m in [("A_WRITE clear", 0x00, 0x01), ("A_HIDDEN set", 0x02, 0x02),
                     ("A_ARCH clear", 0x00, 0x20)]:
        g.chmod(d + "/pd/dd", v, m, "(directory, " + nm + ")")
        lsdir("after " + nm)
    g.cur_tag = None
    g.chmod(d + "/f.txt", 0x01, 0x01)
    g.chmod(d + "/none", 0x00, 0x01, "(missing)")
    g.mkfile(d + "/w1.txt", 102); g.mkfile(d + "/w2.txt", 103)
    g.chmod(d + "/w*.txt", 0x00, 0x01, "(wildcard, A_WRITE clear)")
    g.stat(d + "/w1.txt"); g.stat(d + "/w2.txt")
    g.chmod(d + "/*.zzz", 0x00, 0x01, "(wildcard, no match)")
    g.mkfile(d + "/o.txt", 104)
    g.T("open o.txt write", "F_OPEN", ix=d + "/o.txt", bc=(0x02 << 8), store=0, mask=True)
    g.chmod(d + "/o.txt", 0x00, 0x01, "(file open for write)")
    g.T("F_WRITE 5 more through the handle", "F_WRITE", slot=0, ix=PAT, bc=5, kind=1)
    g.T("close", "F_CLOSE", slot=0)
    g.stat(d + "/o.txt")
    g.chmod(d + "/Long Name.txt", 0x00, 0x01, "(missing)")
    g.mkfile(d + "/Long Name.txt", 105)
    g.chmod(d + "/LONGNA~1.TXT", 0x00, 0x01, "(8.3 name)")
    g.stat(d + "/Long Name.txt")
    g.chmod("C:/", 0x00, 0x01, "(the root)")
    g.chmod("", 0x00, 0x01, "(empty)")

    # ── F_OPEN of a read-only file ───────────────────────────────────────────
    g.group = "openro"
    d = W + "/o"
    g.mkdir(d)
    g.mkfile(d + "/ro.txt", 111); g.ro(d + "/ro.txt")
    for md in [0x01, 0x02, 0x03, 0x04, 0x08, 0x0C, 0x0D, 0x0E]:
        g.cur_tag = "open-mode-no-rw" if md in (0x04, 0x08, 0x0C) else None
        g.open_probe(d + "/ro.txt", md)
        g.cur_tag = None
        g.stat(d + "/ro.txt")

    # ── 8.3 alias stability ──────────────────────────────────────────────────
    g.group = "alias"
    d = W + "/al"
    g.mkdir(d)
    g.mkfile(d + "/Long Name a.txt", 121); g.mkfile(d + "/Long Name b.txt", 122)
    g.mkfile(d + "/Long Name c.txt", 123)
    for t in "123":
        g.stat(d + "/LONGNA~%s.TXT" % t)
    g.T("F_UNLINK LONGNA~1.TXT", "F_UNLINK", ix=d + "/LONGNA~1.TXT")
    for t in "123":
        g.stat(d + "/LONGNA~%s.TXT" % t)
    g.stat(d + "/Long Name a.txt"); g.stat(d + "/Long Name b.txt"); g.stat(d + "/Long Name c.txt")
    g.T("F_UNLINK LONGNA~2.TXT", "F_UNLINK", ix=d + "/LONGNA~2.TXT")
    g.stat(d + "/Long Name b.txt"); g.stat(d + "/Long Name c.txt")
    g.mkfile(d + "/Long Name d.txt", 124)
    for t in "1234":
        g.stat(d + "/LONGNA~%s.TXT" % t)
    g.T("F_RENAME c -> Long Name e.txt", "F_RENAME", ix=d + "/Long Name c.txt", de=d + "/Long Name e.txt")
    for t in "1234":
        g.stat(d + "/LONGNA~%s.TXT" % t)
    g.mkfile(d + "/LONGNA~1.TXT", 125, "(a native 8.3 name that is an alias)")
    for t in "1234":
        g.stat(d + "/LONGNA~%s.TXT" % t)
    g.cur_tag = "alias-create"
    g.stat(d + "/Long Name d.txt")
    g.cur_tag = None

    # ── M_GETERR ────────────────────────────────────────────────────────────
    g.group = "geterr"
    for code in list(range(1, 32)) + [0x20, 0x7F, 0x80, 0xFF]:
        g.T("M_GETERR A=%d B=1" % code, "M_GETERR", a=code, bc=(1 << 8), de=GERR, ix=0, kind=6, addr=GERR)
    g.T("M_GETERR A=5 B=1, buffer prefilled? (second call)", "M_GETERR", a=5, bc=(1 << 8), de=GERR + 64, ix=0, kind=6, addr=GERR + 64)
    g.T("M_GETERR A=0 B=1 IX=custom (bit 7 terminated)", "M_GETERR", a=0, bc=(1 << 8), de=GERR, ix=CUSTOM_MSG, kind=6, addr=GERR)

    # ── the tree the ro phase works on ──────────────────────────────────────
    g.group = "tree"
    d = W + "/RO"
    g.mkdir(d)
    g.mkfile(d + "/f.txt", 21)
    g.mkfile(d + "/ro.txt", 22); g.ro(d + "/ro.txt")
    g.mkdir(d + "/ed")
    g.mkdir(d + "/nd"); g.mkfile(d + "/nd/n.txt", 23)
    g.stat(d + "/f.txt"); g.stat(d + "/ro.txt")

def build_ro(g):
    T = g.T
    g.group = "ro"
    d = W + "/RO"
    T("F_CHDIR C:/", "F_CHDIR", ix="C:/")
    T("F_CHDIR " + d, "F_CHDIR", ix=d)
    g.cwd()
    g.stat(d + "/f.txt"); g.stat(d + "/ro.txt"); g.exists(d + "/ed"); g.exists(d + "/nd/n.txt")
    def U(desc, p): T("F_UNLINK %s (%s)" % (p, desc), "F_UNLINK", ix=p)
    U("file", d + "/f.txt")
    U("missing", d + "/none.txt")
    U("missing dir", d + "/nodir/x.txt")
    U("read-only file", d + "/ro.txt")
    U("directory", d + "/ed")
    U("wildcard", d + "/*.txt")
    U("above the root", "C:/../x")
    T("open f.txt read", "F_OPEN", ix=d + "/f.txt", bc=(0x01 << 8), store=0, mask=True)
    U("an open file", d + "/f.txt")
    T("F_FTRUNCATE the read handle", "F_FTRUNCATE", slot=0, bc=0, de=3)
    T("close", "F_CLOSE", slot=0)
    def R(desc, s, t): T("F_RENAME %s -> %s (%s)" % (s, t, desc), "F_RENAME", ix=s, de=t)
    R("file", d + "/f.txt", d + "/g.txt")
    R("onto an existing file", d + "/f.txt", d + "/ro.txt")
    R("missing source", d + "/none.txt", d + "/g.txt")
    R("into an existing directory", d + "/f.txt", d + "/ed")
    R("read-only file", d + "/ro.txt", d + "/r2.txt")
    R("a directory", d + "/ed", d + "/ed2")
    R("wildcard", d + "/*.txt", d + "/z.txt")
    for nm, p in [("new", d + "/newdir"), ("existing dir", d + "/ed"), ("existing file", d + "/f.txt"),
                  ("missing parent", d + "/no/parent"), ("wildcard", d + "/w*")]:
        T("F_MKDIR %s (%s)" % (p, nm), "F_MKDIR", ix=p)
    for nm, p in [("empty", d + "/ed"), ("non-empty", d + "/nd"), ("file", d + "/f.txt"),
                  ("missing", d + "/none"), ("the CWD", d), ("the root", "C:/")]:
        T("F_RMDIR %s (%s)" % (p, nm), "F_RMDIR", ix=p)
    for nm, p, sz in [("shrink", d + "/f.txt", 5), ("extend", d + "/f.txt", 100), ("same size", d + "/f.txt", 21),
                      ("read-only file", d + "/ro.txt", 5), ("directory", d + "/ed", 5),
                      ("missing", d + "/none", 5), ("wildcard", d + "/*.txt", 5)]:
        g.cur_tag = "ro-truncate" if nm in ("shrink", "extend", "same size", "wildcard") else None
        T("F_TRUNCATE %s to %d (%s)" % (p, sz, nm), "F_TRUNCATE", ix=p, bc=0, de=sz)
        g.cur_tag = None
    for nm, p, v, m in [("A_WRITE clear", d + "/f.txt", 0, 1), ("A_WRITE set", d + "/ro.txt", 1, 1),
                        ("A_ARCH set", d + "/f.txt", 0x20, 0x20), ("mask 0", d + "/f.txt", 1, 0),
                        ("missing", d + "/none", 0, 1), ("directory", d + "/ed", 0, 1),
                        ("wildcard", d + "/*.txt", 0, 1)]:
        g.chmod(p, v, m, "(" + nm + ")")
    g.cur_tag = "ro-open-write"
    for md in [0x02, 0x03, 0x0C, 0x0E, 0x04, 0x08]:
        g.open_probe(d + "/f.txt", md, "(f.txt)")
    g.cur_tag = None
    g.open_probe(d + "/f.txt", 0x01, "(f.txt)")
    g.open_probe(d + "/ro.txt", 0x02, "(read-only file)")
    g.cur_tag = "ro-open-write"
    g.open_probe(d + "/new.txt", 0x0E, "(create)")
    g.open_probe(d + "/new.txt", 0x04, "(create, must not exist)")
    g.cur_tag = None
    g.exists(d + "/new.txt")
    g.stat(d + "/f.txt"); g.stat(d + "/ro.txt"); g.exists(d + "/g.txt"); g.exists(d + "/newdir")
    g.exists(d + "/ed"); g.exists(d + "/ed2"); g.exists(d + "/nd/n.txt")
    for code in (24, 31, 9, 8):
        T("M_GETERR A=%d" % code, "M_GETERR", a=code, bc=(1 << 8), de=GERR, kind=6, addr=GERR)

CUSTOM_MSG = 0x8780

# ── layout and NEX writer ────────────────────────────────────────────────────
def layout(g):
    strings = {}
    order = []
    for s in g.steps:
        for v in (s.ix, s.de):
            if isinstance(v, str) and v not in strings:
                strings[v] = None; order.append(v)
    tsize = (len(g.steps) + 1) * 14
    ssize = sum(len(v.encode("latin-1")) + 1 for v in order)
    # the table (14 bytes a step) goes to bank 0 at $C000; the strings to bank 2
    base = BANK0
    assert base + tsize <= 0x10000, "table does not fit in bank 0"
    assert TABLE_LO + ssize <= 0xBE00, "strings do not fit in bank 2"
    p = TABLE_LO
    for v in order:
        strings[v] = p; p += len(v.encode("latin-1")) + 1
    return base, strings, order

def emit_table(g, base, strings):
    out = bytearray()
    def val(v):
        return strings[v] if isinstance(v, str) else v
    for s in g.steps:
        flags = (1 if s.slot is not None else 0) | (2 if s.store is not None else 0) | \
                (4 if s.mask else 0) | (8 if s.close else 0)
        out += struct.pack("<BBBBHHHBBH", s.hook,
                           (s.slot if s.slot is not None else s.a) & 255, flags,
                           (s.store or 0) & 255, val(s.ix) & 0xFFFF, val(s.de) & 0xFFFF,
                           s.bc & 0xFFFF, s.kind, s.ln, s.addr)
    out += b"\0" * 14
    return out

def make_nex(mode):
    g = build(mode)
    base, strings, order = layout(g)
    items.clear()
    program()
    # pass 1
    pc = CODE_ORG
    for it in items:
        if isinstance(it, bytes): pc += len(it)
        elif it[0] == "label": labels[it[1]] = pc
        elif it[0] == "rel": pc += 1
        elif it[0] == "w16": pc += 2
        elif it[0] == "tablehl": pc += 3
    out = bytearray(); pc = CODE_ORG
    for it in items:
        if isinstance(it, bytes): out += it; pc += len(it)
        elif it[0] == "label": pass
        elif it[0] == "rel":
            pc += 1; dlt = labels[it[1]] - pc
            assert -128 <= dlt <= 127, (it[1], dlt)
            out.append(dlt & 255)
        elif it[0] == "w16": out += struct.pack("<H", labels[it[1]]); pc += 2
        elif it[0] == "tablehl": out += bytes([0x21, base & 255, base >> 8]); pc += 3
    assert len(out) < 0x400, len(out)
    mem = {}      # address -> bytes
    bank2 = bytearray(16384); bank0 = bytearray(16384)
    def put(addr, raw):
        if addr < 0xC000: bank2[addr - 0x8000:addr - 0x8000 + len(raw)] = raw
        else: bank0[addr - 0xC000:addr - 0xC000 + len(raw)] = raw
    put(CODE_ORG, bytes(out))
    put(PAT, bytes((i * 7 + 3) & 255 for i in range(512)))
    put(CUSTOM_MSG, b"Custom messagE")          # last character has bit 7 set below
    put(CUSTOM_MSG + 13, bytes([ord("E") | 0x80]))
    put(base, bytes(emit_table(g, base, strings)))
    for v in order:
        put(strings[v], v.encode("latin-1") + b"\0")
    h = bytearray(512)
    h[0:4] = b"Next"; h[4:8] = b"V1.2"
    h[9] = 2
    h[12:14] = struct.pack("<H", 0xBFF0)
    h[14:16] = struct.pack("<H", 0x8000)
    h[18 + 2] = 1
    h[18 + 0] = 1
    body = bytes(h) + bytes(bank2) + bytes(bank0)   # NEX file order: 5, 2, 0, 1, 3, ...
    return g, body

def read_transcript(path, mode=None):
    """step number -> text. A recording holds a [rw] and a [ro] section; a raw run has none."""
    d = {}
    sect = None
    for ln in open(path):
        ln = ln.rstrip("\n")
        if ln.startswith("["):
            sect = ln.strip("[]"); continue
        if not ln.startswith("S") or (mode and sect and sect != mode): continue
        body = ln.split(";")[0].strip()
        parts = body.split(None, 1)
        d[int(parts[0][1:], 16)] = parts[1] if len(parts) > 1 else ""
    return d

# Declared divergences: step description tag substring -> reason. Filled from the
# measurement (see the fixture README); a step not listed here that differs is a failure.
DIVERGE = {
    "bare-dotdot-stat": "F_STAT of a bare '..' above the root keeps the esx_epath (19) it has always answered (the "
                        "HFS rows pin it); NextZXOS says esx_enoent (5)",
    "cwd-follows-rename": "a renamed current directory shows its NEW name in F_GETCWD; NextZXOS keeps showing the old one",
    "trailing-space": "a name ending in a space is refused with esx_einval: NextZXOS keeps the space, a host may strip it",
    "chmod-unserved": "an F_CHMOD change the host cannot store (hidden, system, archive cleared) is refused with "
                      "esx_enosys, refused and not faked; the state after it is the unchanged one",
    "chmod-dir": "an F_CHMOD of a directory that would change an attribute is refused with esx_enosys: a host "
                 "directory has no stored attribute for F_READDIR to show",
    "alias-create": "creating through the 8.3 alias of an existing long-named file rewrites that FAT entry and loses "
                    "the long name; jnext truncates the file it names and keeps its host name",
    "open-mode-no-rw": "an F_OPEN mode with no read and no write bit is esx_einval here (existing behaviour); "
                       "NextZXOS opens or creates",
    "ro-truncate": "F_TRUNCATE on a read-only root is refused with esx_edevicebusy; NextZXOS answers Fc=0 and loses "
                   "the write",
    "ro-open-write": "F_OPEN for writing or creating on a read-only root is esx_erdonly (existing behaviour); "
                     "NextZXOS opens a file for writing and refuses a create with esx_edevicebusy",
}


def main():
    a = sys.argv[1:]
    cmd = a.pop(0)
    mode = a[a.index("--mode") + 1]; i = a.index("--mode"); del a[i:i + 2]
    rec = None
    if "--recorded" in a:
        i = a.index("--recorded"); rec = a[i + 1]; del a[i:i + 2]
    if cmd == "nex":
        g, body = make_nex(mode)
        open(a[0], "wb").write(body)
    elif cmd == "steps":
        g = build(mode)
        for n, s in enumerate(g.steps):
            print("S%04X %-12s %s" % (n, NAME.get(s.hook, hex(s.hook)), s.desc))
    elif cmd == "annotate":
        g = build(mode)
        got = read_transcript(a[0])
        for n, s in enumerate(g.steps):
            print("S%04X %s ; %s" % (n, got.get(n, "MISSING"), s.desc))
        print("END" if any(l.startswith("END") for l in open(a[0])) else "NO-END")
    elif cmd == "compare":
        g = build(mode)
        want = read_transcript(rec, mode); got = read_transcript(a[0])
        bad = 0
        if not any(l.startswith("END") for l in open(a[0])):
            print("no END line: the probe did not finish"); bad += 1
        for n, s in enumerate(g.steps):
            if s.hdr: continue
            if want.get(n) != got.get(n):
                key = [k for k in DIVERGE if k == s.tag or k in s.desc]
                if key:
                    print("declared  S%04X %s: recorded '%s' jnext '%s' (%s)" % (n, s.desc, want.get(n), got.get(n), DIVERGE[key[0]]))
                else:
                    print("DIFFERS   S%04X %s: recorded '%s' got '%s'" % (n, s.desc, want.get(n), got.get(n)))
                    bad += 1
        sys.exit(1 if bad else 0)
    else:
        sys.exit("unknown command")

if __name__ == "__main__":
    main()
