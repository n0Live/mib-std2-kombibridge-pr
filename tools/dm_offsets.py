#!/usr/bin/env python3
"""
dm_offsets.py - find the addresses libkb_dm.so needs in ONE firmware build and write them as a C header.

libkb_dm.so (native/src/dm_hook.c) is tied to two files of the unit:

  libOpenMAXAL.so.1                 the buffer-queue functions mmfbq_get / mmfbq_done /
                                    mmfbq_get_and_wait, and the one call of mmfbq_get_and_wait that
                                    belongs to DisplayManager's cluster video reader
  tsd.mibstd2.system.displaymanager the one call of RingIO::writeAquire (in MostSink::write)

The firmware differs in where these are. This tool reads the two files of a dump (a folder holding the
files as they are on the unit, taken anywhere below it by name), finds the places and writes
native/include/dm_offsets.h's replacement. native/build.sh gives the result to the compiler first,
so the header that is checked in (firmware 0480) is only the default.

    python3 tools/dm_offsets.py <dump folder> [-o dm_offsets.h]

Everything is found by structure, never by a fixed address:
  * libOpenMAXAL: the exported mmfbq_* symbols; the reader's call = the `bl` to mmfbq_get_and_wait
    (direct, or through its PLT stub) inside BufferQueueGetAndWait (local symbol, when the file still
    has its symbol table), otherwise the one such call that is followed by a `pop {..., pc}`.
  * displaymanager (stripped): the PLT stub of RingIO::writeAquire (from .rel.plt), and the `bl` to it.
    More than one caller: the one inside a method of MostSink's vtable.
The words found at the call sites are written too: the hook checks them at run time, and a mismatch
there (a build this tool was not given) leaves the hook inert.

What cannot be read from the files - the layout of the queue and its elements (head at +0, next at +0x18,
data at +4, size at +8) - stays as dm_hook.c assumes it. Needs Python 3 and pyelftools (the build
container has both).
"""
import argparse
import bisect
import hashlib
import io
import os
import struct
import sys

try:
    from elftools.elf.elffile import ELFFile
except ImportError:                                  # pragma: no cover
    sys.exit("dm_offsets.py needs pyelftools (pip install pyelftools; the build container has it)")

OMX_NAME = "libOpenMAXAL.so.1"
DM_NAME = "tsd.mibstd2.system.displaymanager"
READER_FUNC = "BufferQueueGetAndWait"
ACQUIRE_SYMBOL = "_ZN3tsd6common9messaging6RingIO11writeAquireERPvRjb"
VTABLE_SYMBOL = "_ZTVN3tsd7mibstd26system14displaymanager8MostSinkE"
ARM_PUSH_LR = 0xE52DE004                              # first word of a standard ARM PLT header


class DumpError(Exception):
    pass


def is_bl(word):
    return (word >> 24) == 0xEB                       # bl, condition "always"


def bl_target(addr, word):
    off = word & 0xFFFFFF
    if off & 0x800000:
        off -= 1 << 24
    return addr + 8 + off * 4


def is_pop_pc(word):
    return (word & 0xFFFF0000) == 0xE8BD0000 and bool(word & 0x8000)    # ldm sp!, {..., pc}


def rot_imm(word):
    """The value of an ARM data-processing immediate operand (8 bits rotated right)."""
    v, r = word & 0xFF, ((word >> 8) & 0xF) * 2
    return ((v >> r) | (v << (32 - r))) & 0xFFFFFFFF if r else v


class Image:
    """An ELF32 ARM file: words by virtual address, symbols, PLT stubs, call sites."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        try:
            self.elf = ELFFile(io.BytesIO(self.data))
            self.secs = {s.name: s for s in self.elf.iter_sections()}
            self.alloc = [(s["sh_addr"], s["sh_size"], s["sh_offset"]) for s in self.secs.values()
                          if s["sh_addr"] and s["sh_type"] != "SHT_NOBITS"]
            self._cache_relocs()
        except Exception as e:                        # pyelftools raises several types on a bad file
            raise DumpError("%s: not an ELF file I can read (%s)" % (path, e))
        if self.elf.header["e_machine"] != "EM_ARM" or self.elf.elfclass != 32:
            raise DumpError("%s: not a 32-bit ARM ELF file" % path)

    def _cache_relocs(self):
        self.plt_relocs = {}
        rel = self.secs.get(".rel.plt")
        dyn = self.secs.get(".dynsym")
        if rel is not None and dyn is not None:
            for r in rel.iter_relocations():
                self.plt_relocs[r["r_offset"]] = dyn.get_symbol(r["r_info_sym"]).name

    def word(self, va):
        for a, size, off in self.alloc:
            if a <= va and va + 4 <= a + size:
                return struct.unpack_from("<I", self.data, off + va - a)[0]
        raise DumpError("%s: address 0x%x is not in the file" % (self.path, va))

    def dynsyms(self):
        dyn = self.secs.get(".dynsym")
        if dyn is None:
            raise DumpError("%s: no dynamic symbol table" % self.path)
        return {s.name: s for s in dyn.iter_symbols() if s["st_shndx"] != "SHN_UNDEF" and s.name}

    def local_symbol(self, name):
        sym = self.secs.get(".symtab")
        if sym is None:
            return None
        for s in sym.iter_symbols():
            if s.name == name and s["st_value"]:
                return s["st_value"] & ~1, s["st_size"]
        return None

    def plt_stubs(self, symbol):
        """Addresses of the PLT stubs that jump through the GOT slot of <symbol>."""
        plt = self.secs.get(".plt")
        if plt is None:
            return []
        base, end = plt["sh_addr"], plt["sh_addr"] + plt["sh_size"]
        start = base + 20 if self.word(base) == ARM_PUSH_LR else base
        found = []
        for a in range(start, end - 11, 12):
            w0, w1, w2 = self.word(a), self.word(a + 4), self.word(a + 8)
            if (w0 & 0xFFFF0000) != 0xE28F0000 or (w1 & 0xFFFF0000) != 0xE28C0000 or (w2 & 0xFFFF0000) != 0xE5BC0000:
                continue                              # add ip,pc,# ; add ip,ip,# ; ldr pc,[ip,#]!
            got = a + 8 + rot_imm(w0) + rot_imm(w1) + (w2 & 0xFFF)
            if self.plt_relocs.get(got) == symbol:
                found.append(a)
        return found

    def bl_sites(self, targets):
        text = self.secs.get(".text")
        if text is None:
            raise DumpError("%s: no .text section" % self.path)
        base, off = text["sh_addr"], text["sh_offset"]
        n = text["sh_size"] // 4
        words = struct.unpack_from("<%dI" % n, self.data, off)
        out = []
        for i, w in enumerate(words):
            if is_bl(w) and bl_target(base + i * 4, w) in targets:
                out.append(base + i * 4)
        return out

    def function_starts(self):
        """Function start addresses from .ARM.exidx (prel31 entries), sorted."""
        ex = self.secs.get(".ARM.exidx")
        if ex is None:
            return []
        starts = []
        for i in range(0, ex["sh_size"], 8):
            v = struct.unpack_from("<I", self.data, ex["sh_offset"] + i)[0] & 0x7FFFFFFF
            if v & 0x40000000:
                v -= 0x80000000
            starts.append((ex["sh_addr"] + i + v) & 0xFFFFFFFF)
        return sorted(starts)


def find_file(folder, name):
    hits = []
    for root, _dirs, files in os.walk(folder):
        if name in files:
            hits.append(os.path.join(root, name))
    if not hits:
        raise DumpError("%s: not found below %s" % (name, folder))
    return sorted(hits)[0]


def analyse_omx(img):
    syms = img.dynsyms()
    for n in ("mmfbq_get", "mmfbq_done", "mmfbq_get_and_wait"):
        if n not in syms:
            raise DumpError("%s: no exported %s" % (img.path, n))
    gw = syms["mmfbq_get_and_wait"]["st_value"]
    targets = {gw} | set(img.plt_stubs("mmfbq_get_and_wait"))
    sites = img.bl_sites(targets)
    func = img.local_symbol(READER_FUNC)
    if func:
        cand = [a for a in sites if func[0] <= a < func[0] + func[1]]
        how = "inside " + READER_FUNC
    else:
        cand = [a for a in sites if is_pop_pc(img.word(a + 4))]
        how = "the call followed by pop {..., pc} (no symbol table)"
    if len(cand) != 1:
        raise DumpError("%s: %d candidate calls of mmfbq_get_and_wait for the reader (%s); calls at %s"
                        % (img.path, len(cand), how, ", ".join("0x%x" % a for a in sites)))
    call = cand[0]
    return {
        "OMX_GET": syms["mmfbq_get"]["st_value"],
        "OMX_DONE": syms["mmfbq_done"]["st_value"],
        "OMX_GET_AND_WAIT": gw,
        "OMX_READER_RETURN": call + 4,
        "OMX_RETURN_WORD": img.word(call + 4),
    }


def vtable_methods(img):
    """The code addresses in MostSink's vtable (the executable's own data holds them directly)."""
    sym = img.dynsyms().get(VTABLE_SYMBOL)
    if sym is None:
        return set()
    out = set()
    for a in range(sym["st_value"], sym["st_value"] + sym["st_size"], 4):
        try:
            out.add(img.word(a) & ~1)
        except DumpError:
            pass
    return out


def analyse_dm(img):
    stubs = img.plt_stubs(ACQUIRE_SYMBOL)
    if not stubs:
        raise DumpError("%s: no PLT stub for RingIO::writeAquire" % img.path)
    sites = img.bl_sites(set(stubs))
    if len(sites) > 1:
        methods = vtable_methods(img)
        starts = img.function_starts()
        keep = []
        for a in sites:
            k = bisect.bisect_right(starts, a) - 1
            if k >= 0 and starts[k] in methods:
                keep.append(a)
        sites = keep
    if len(sites) != 1:
        raise DumpError("%s: %d calls of RingIO::writeAquire that could be MostSink::write's"
                        % (img.path, len(sites)))
    call = sites[0]
    return {
        "MOST_SINK_CALL": call,
        "MOST_SINK_RETURN": call + 4,
        "MOST_SINK_CALL_WORD": img.word(call),
        "MOST_SINK_RETURN_WORD": img.word(call + 4),
    }


def header_text(omx, dm, omx_img, dm_img, note):
    lines = [
        ("/* Generated by tools/dm_offsets.py - do not edit.%s */" % ((" " + note) if note else "")),
        "#ifndef DM_OFFSETS_H",
        "#define DM_OFFSETS_H",
        "",
        "/* libOpenMAXAL.so.1 %s (load-base relative) */" % omx_img.sha256,
        '#define DM_OMX_SHA256      "%s"' % omx_img.sha256,
    ]
    for k in ("OMX_GET", "OMX_DONE", "OMX_GET_AND_WAIT", "OMX_READER_RETURN"):
        lines.append("#define %-18s 0x%xu" % (k, omx[k]))
    lines.append("#define %-18s 0x%08xu" % ("OMX_RETURN_WORD", omx["OMX_RETURN_WORD"]))
    lines += ["", "/* %s %s (absolute addresses: it is not position independent) */" % (DM_NAME, dm_img.sha256),
              '#define DM_DM_SHA256       "%s"' % dm_img.sha256]
    for k in ("MOST_SINK_CALL", "MOST_SINK_RETURN"):
        lines.append("#define %-22s 0x%xu" % (k, dm[k]))
    for k in ("MOST_SINK_CALL_WORD", "MOST_SINK_RETURN_WORD"):
        lines.append("#define %-22s 0x%08xu" % (k, dm[k]))
    lines += ["", '#define DM_OFFSETS_NOTE "libOpenMAXAL %s, displaymanager %s"' % (omx_img.sha256[:8], dm_img.sha256[:8]),
              "", "#endif", ""]
    return "\n".join(lines)


def generate(folder, note=""):
    omx_img = Image(find_file(folder, OMX_NAME))
    dm_img = Image(find_file(folder, DM_NAME))
    return header_text(analyse_omx(omx_img), analyse_dm(dm_img), omx_img, dm_img, note), omx_img, dm_img


def main(argv=None):
    ap = argparse.ArgumentParser(description="addresses libkb_dm.so needs, from a firmware dump")
    ap.add_argument("dump", help="folder with libOpenMAXAL.so.1 and tsd.mibstd2.system.displaymanager")
    ap.add_argument("-o", "--out", help="write the header here (default: print)")
    ap.add_argument("--note", default="", help="extra text for the header's first line")
    args = ap.parse_args(argv)
    try:
        text, omx_img, dm_img = generate(args.dump, args.note)
    except DumpError as e:
        sys.stderr.write("dm_offsets.py: %s\n" % e)
        return 2
    sys.stderr.write("dm_offsets.py: libOpenMAXAL %s, displaymanager %s\n" % (omx_img.sha256[:8], dm_img.sha256[:8]))
    if args.out:
        with open(args.out, "w", newline="\n") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
