import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import dm_offsets as D  # noqa: E402

BL = 0xEB000000
NOP = 0xE1A00000
MOV_R7_R0 = 0xE1A07000
POP_R4_PC = 0xE8BD8010


def bl(frm, to):
    return BL | (((to - (frm + 8)) // 4) & 0xFFFFFF)


def plt_stub(addr, got):
    d = got - (addr + 8)
    assert d >= 0 and (d >> 12) <= 0xFF
    return [0xE28FC600, 0xE28CCA00 | (d >> 12), 0xE5BCF000 | (d & 0xFFF)]


class Elf:
    """A minimal 32-bit little-endian ARM ELF: named sections, dynamic and ordinary symbols."""
    BASE = 0x10000

    def __init__(self):
        self.secs = []       # [name, type, flags, data, link, info, entsize, addr]

    def add(self, name, typ, data=b"", flags=0, link=0, info=0, entsize=0):
        self.secs.append([name, typ, flags, data, link, info, entsize, 0])
        return len(self.secs)    # section index (the null section is 0)

    @staticmethod
    def strtab(names):
        data, offs = b"\0", {}
        for n in names:
            offs[n] = len(data)
            data += n.encode() + b"\0"
        return data, offs

    @staticmethod
    def symtab(syms, offs):
        out = b"\0" * 16
        for name, value, size, shndx in syms:
            out += struct.pack("<IIIBBH", offs[name], value, size, 0x12, 0, shndx)
        return out

    def build(self):
        names = [s[0] for s in self.secs] + [""]
        shstr, shoff = self.strtab(n for n in names if n)
        self.add(".shstrtab", 3, shstr)
        off = 52
        placed = []
        for s in self.secs:
            off = (off + 3) & ~3
            placed.append(off)
            s[7] = self.BASE + off if s[2] & 2 else 0
            off += len(s[3])
        off = (off + 3) & ~3
        blob = bytearray(off)
        for s, o in zip(self.secs, placed):
            blob[o:o + len(s[3])] = s[3]
        sh = b"\0" * 40
        for s, o in zip(self.secs, placed):
            sh += struct.pack("<IIIIIIIIII", shoff.get(s[0], 0), s[1], s[2], s[7], o, len(s[3]), s[4], s[5], 4, s[6])
        hdr = b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\0" * 8
        hdr += struct.pack("<HHIIIIIHHHHHH", 2, 40, 1, 0, 0, off, 0, 52, 0, 0, 40, len(self.secs) + 1, len(self.secs))
        blob[0:52] = hdr
        return bytes(blob) + sh

    def addr_of(self, name):
        for s in self.secs:
            if s[0] == name:
                return s[7]
        raise KeyError(name)


def words(*ws):
    return struct.pack("<%dI" % len(ws), *ws)


def make_omx(path, symtab=True, decoy_pop=False):
    """libOpenMAXAL stand-in: reader (bl gw ; pop), a decoy via the PLT stub, optionally a second pop."""
    # layout is fixed so that addresses can be computed before the sections exist: text starts at TEXT
    e = Elf()
    # sizes: dynstr/dynsym/rel.plt/plt come first; text address is known after a trial layout
    for trial in range(2):
        e = Elf()
        dynnames = ["mmfbq_get", "mmfbq_done", "mmfbq_get_and_wait"]
        dstr, doff = e.strtab(dynnames)
        text_addr = TEXT_ADDR[0]
        gw, get, done, reader, other = text_addr, text_addr + 0x10, text_addr + 0x20, text_addr + 0x40, text_addr + 0x80
        syms = [("mmfbq_get", get, 16, 0), ("mmfbq_done", done, 16, 0), ("mmfbq_get_and_wait", gw, 16, 0)]
        text_idx = 5   # .dynsym .dynstr .rel.plt .plt .text -> 1..5
        syms = [(n, v, s, text_idx) for n, v, s, _ in syms]
        e.add(".dynsym", 11, e.symtab(syms, doff), 2, link=2, info=1, entsize=16)
        e.add(".dynstr", 3, dstr, 2)
        e.add(".rel.plt", 9, struct.pack("<II", 0x30000, (3 << 8) | 22), 2, link=1, entsize=8)
        plt_addr_guess = PLT_ADDR[0]
        plt = words(0xE52DE004, 0xE59FE004, 0xE08FE00E, 0xE5BEF008, 0x1000)
        plt += words(*plt_stub(plt_addr_guess + 20, 0x30000))
        e.add(".plt", 1, plt, 6)
        text = bytearray(0x100)
        def put(addr, *ws):
            o = addr - text_addr
            text[o:o + 4 * len(ws)] = words(*ws)
        put(gw, NOP, NOP)
        put(reader, NOP, NOP, NOP, NOP, bl(reader + 16, gw), POP_R4_PC)           # reader+16 = call
        put(other, bl(other, plt_addr_guess + 20), MOV_R7_R0)                      # decoy through the stub
        if decoy_pop:
            put(other + 0x20, bl(other + 0x20, gw), POP_R4_PC)
        e.add(".text", 1, bytes(text), 6)
        if symtab:
            names = ["BufferQueueGetAndWait"]
            sstr, soff = e.strtab(names)
            e.add(".symtab", 2, e.symtab([("BufferQueueGetAndWait", reader, 0x20, text_idx)], soff), 0, link=len(e.secs) + 2, info=1, entsize=16)
            e.add(".strtab", 3, sstr)
        if trial == 0:
            blob = e.build()
            PLT_ADDR[0] = e.addr_of(".plt")
            TEXT_ADDR[0] = e.addr_of(".text")
    blob = e.build()
    assert e.addr_of(".plt") == PLT_ADDR[0] and e.addr_of(".text") == TEXT_ADDR[0]
    with open(path, "wb") as f:
        f.write(blob)
    return {"gw": gw, "get": get, "done": done, "return": reader + 20}


TEXT_ADDR = [Elf.BASE + 0x400]
PLT_ADDR = [Elf.BASE + 0x200]


def make_dm(path, extra_call=False, no_call=False):
    """DisplayManager stand-in: PLT with writeAquire and another import, MostSink vtable, exidx."""
    for trial in range(2):
        e = Elf()
        dyn = ["_ZN3tsd6common9messaging6RingIO11writeAquireERPvRjb", "other_import", D.VTABLE_SYMBOL]
        dstr, doff = e.strtab(dyn)
        text_addr, plt_addr, data_addr, exidx_addr = TEXT_ADDR[0], PLT_ADDR[0], DATA_ADDR[0], EXIDX_ADDR[0]
        write, plain, thunk = text_addr, text_addr + 0x40, text_addr + 0x80
        syms = [(dyn[0], 0, 0, 0), (dyn[1], 0, 0, 0), (dyn[2], data_addr, 8, 6)]
        e.add(".dynsym", 11, e.symtab(syms, doff), 2, link=2, info=1, entsize=16)
        e.add(".dynstr", 3, dstr, 2)
        e.add(".rel.plt", 9, struct.pack("<IIII", 0x30000, (1 << 8) | 22, 0x30004, (2 << 8) | 22), 2, link=1, entsize=8)
        stub_wa, stub_other = plt_addr + 20, plt_addr + 32
        plt = words(0xE52DE004, 0xE59FE004, 0xE08FE00E, 0xE5BEF008, 0x1000)
        plt += words(*plt_stub(stub_wa, 0x30000)) + words(*plt_stub(stub_other, 0x30004))
        e.add(".plt", 1, plt, 6)
        text = bytearray(0x100)
        def put(addr, *ws):
            o = addr - text_addr
            text[o:o + 4 * len(ws)] = words(*ws)
        if no_call:
            put(write, NOP, NOP, NOP)
        else:
            put(write, NOP, bl(write + 4, stub_wa), 0xE2509000)
        put(plain, bl(plain, stub_other) if not extra_call else bl(plain, stub_wa), NOP)
        put(thunk, NOP)
        e.add(".text", 1, bytes(text), 6)
        e.add(".data", 1, words(write, thunk), 3)
        ex = b""
        for i, fn in enumerate([write, plain, thunk]):
            at = exidx_addr + 8 * i
            ex += struct.pack("<II", (fn - at) & 0x7FFFFFFF, 1)
        e.add(".ARM.exidx", 0x70000001, ex, 2)
        if trial == 0:
            e.build()
            PLT_ADDR[0], TEXT_ADDR[0] = e.addr_of(".plt"), e.addr_of(".text")
            DATA_ADDR[0], EXIDX_ADDR[0] = e.addr_of(".data"), e.addr_of(".ARM.exidx")
    with open(path, "wb") as f:
        f.write(e.build())
    return {"call": write + 4, "return": write + 8}


DATA_ADDR = [Elf.BASE + 0x800]
EXIDX_ADDR = [Elf.BASE + 0x900]


class T(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def omx(self, **kw):
        # the layout depends on section sizes: build until the guessed addresses are stable
        for _ in range(3):
            exp = make_omx(os.path.join(self.dir, "o.so"), **kw)
        return exp, D.analyse_omx(D.Image(os.path.join(self.dir, "o.so")))

    def test_omx_with_symbols(self):
        exp, got = self.omx()
        self.assertEqual(got["OMX_GET"], exp["get"])
        self.assertEqual(got["OMX_DONE"], exp["done"])
        self.assertEqual(got["OMX_GET_AND_WAIT"], exp["gw"])
        self.assertEqual(got["OMX_READER_RETURN"], exp["return"])
        self.assertEqual(got["OMX_RETURN_WORD"], POP_R4_PC)

    def test_omx_without_symbol_table_uses_the_pop(self):
        exp, got = self.omx(symtab=False)
        self.assertEqual(got["OMX_READER_RETURN"], exp["return"])

    def test_omx_ambiguous_without_symbol_table(self):
        with self.assertRaises(D.DumpError):
            self.omx(symtab=False, decoy_pop=True)

    def test_omx_two_calls_but_the_function_is_known(self):
        exp, got = self.omx(symtab=True, decoy_pop=True)
        self.assertEqual(got["OMX_READER_RETURN"], exp["return"])

    def dm(self, **kw):
        for _ in range(3):
            exp = make_dm(os.path.join(self.dir, "d"), **kw)
        return exp, D.Image(os.path.join(self.dir, "d"))

    def test_dm_single_call(self):
        exp, img = self.dm()
        got = D.analyse_dm(img)
        self.assertEqual(got["MOST_SINK_CALL"], exp["call"])
        self.assertEqual(got["MOST_SINK_RETURN"], exp["return"])
        self.assertEqual(got["MOST_SINK_RETURN_WORD"], 0xE2509000)
        self.assertTrue(D.is_bl(got["MOST_SINK_CALL_WORD"]))

    def test_dm_two_calls_the_vtable_method_wins(self):
        exp, img = self.dm(extra_call=True)
        self.assertEqual(D.analyse_dm(img)["MOST_SINK_CALL"], exp["call"])

    def test_dm_no_call(self):
        _exp, img = self.dm(no_call=True)
        with self.assertRaises(D.DumpError):
            D.analyse_dm(img)

    def test_generate_and_cli(self):
        sub = os.path.join(self.dir, "dump", "usr", "lib")
        os.makedirs(sub)
        for _ in range(3):
            make_omx(os.path.join(sub, D.OMX_NAME))
            make_dm(os.path.join(self.dir, "dump", D.DM_NAME))
        out = os.path.join(self.dir, "o.h")
        self.assertEqual(D.main([os.path.join(self.dir, "dump"), "-o", out]), 0)
        text = read(out)
        for key in ("OMX_GET ", "OMX_READER_RETURN", "OMX_RETURN_WORD", "MOST_SINK_CALL_WORD", "DM_OMX_SHA256", "DM_OFFSETS_NOTE"):
            self.assertIn(key, text)

    def test_missing_file_is_reported(self):
        self.assertEqual(D.main([self.dir]), 2)

    def test_not_an_elf(self):
        p = os.path.join(self.dir, D.OMX_NAME)
        with open(p, "wb") as f:
            f.write(b"not an elf")
        with self.assertRaises(D.DumpError):
            D.Image(p)


def read(path, mode="r"):
    with open(path, mode) as f:
        return f.read()


def defines(text):
    out = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "#define":
            out[parts[1]] = parts[2]
    return out


class RealDump(unittest.TestCase):
    """The addresses found in the real firmware 0480 files must equal the checked-in defaults."""

    def test_default_header_matches_the_dump(self):
        folder = os.environ.get("DM_REAL_DUMP") or os.path.join(ROOT, "research", "dm")
        if not os.path.isdir(folder):
            self.skipTest("no real dump at %s" % folder)
        text, omx, _dm = D.generate(folder)
        default = defines(read(os.path.join(ROOT, "native", "include", "dm_offsets.h")))
        if default.get("DM_OMX_SHA256", "").strip('"') != omx.sha256:
            self.skipTest("the dump is another firmware than the default header")
        self.assertEqual({k: v for k, v in defines(text).items() if k != "DM_OFFSETS_NOTE"},
                         {k: v for k, v in default.items() if k != "DM_OFFSETS_NOTE"})


class DefaultHeader(unittest.TestCase):
    def test_all_names_the_hook_uses_exist(self):
        names = defines(read(os.path.join(ROOT, "native", "include", "dm_offsets.h")))
        for k in ("OMX_GET", "OMX_DONE", "OMX_GET_AND_WAIT", "OMX_READER_RETURN", "OMX_RETURN_WORD",
                  "MOST_SINK_CALL", "MOST_SINK_RETURN", "MOST_SINK_CALL_WORD", "MOST_SINK_RETURN_WORD",
                  "DM_OFFSETS_NOTE", "DM_OMX_SHA256", "DM_DM_SHA256"):
            self.assertIn(k, names)


if __name__ == "__main__":
    unittest.main(verbosity=1)
