#!/usr/bin/env python3
"""Worst-case stack depth of a FREE-WILi 2 (RP2350, Cortex-M33) app.

Input is an ELF built by hwcheck/ (so every object was compiled with GCC's
-fcallgraph-info=su), the linker map next to it (NAME.elf.map) and the .ci
files next to the objects. The .ci files give each C function's frame size and
its direct calls. Functions without a .ci entry (assembly, prebuilt newlib /
libgcc) are estimated from the disassembly: prologue pushes and SP
adjustments, plus their `bl`/tail-call targets.

What is computed:
  * main            deepest call chain from main() (thread mode, core 0)
  * IRQ handlers    handlers installed with irq_set_exclusive_handler() /
                    irq_add_shared_handler() (found as function-address
                    literals in the function that calls them), vector-table
                    entries that point at C code, and callbacks run from SDK
                    IRQ dispatchers (alarms, repeating timers, GPIO callbacks).
                    Indirect calls made in IRQ context are taken to reach any
                    of those callbacks.
  * combined        main + exception entry frame + worst handler. On hardware
                    an interrupt stacks on top of whatever it interrupted, on
                    the same MSP. All SDK IRQs default to the same priority,
                    so handlers do not nest unless the app changes priorities
                    (reported when irq_set_priority() is called).
  * core 1          the entry passed to multicore_launch_core1*() if any.

Anything that makes a figure a lower bound is reported as "unknown": indirect
calls, recursion, dynamically sized frames (alloca / VLAs) and functions with
no stack information at all.

Only the Python standard library and arm-none-eabi-objdump are used.

    tools/stackcheck.py build-hw/apps/hello_display/hello_display.elf [--json]
"""

import argparse
import bisect
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from collections import defaultdict

# --------------------------------------------------------------------- ELF


class Elf:
    """Minimal ELF32 little-endian reader: sections, segments, symbols."""

    SHT_NOBITS = 8
    SHT_SYMTAB = 2
    SHF_ALLOC = 0x2
    PT_LOAD = 1

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            d = f.read()
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
            raise ValueError(f"{path}: not a 32-bit little-endian ELF")
        (_, _, _, _, phoff, shoff, _, _, phentsize, phnum, shentsize, shnum,
         shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", d, 16)
        raw = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
        strtab = raw[shstrndx]

        def cstr(off):
            end = d.index(b"\0", off)
            return d[off:end].decode("utf-8", "replace")

        self.sections = []
        for (name, typ, flags, addr, off, size, link, info, align, entsize) in raw:
            self.sections.append(dict(name=cstr(strtab[4] + name), type=typ, flags=flags,
                                      addr=addr, offset=off, size=size, link=link, entsize=entsize))
        self.segments = []
        for i in range(phnum):
            (typ, off, vaddr, paddr, filesz, memsz, flags, align) = struct.unpack_from(
                "<IIIIIIII", d, phoff + i * phentsize)
            self.segments.append(dict(type=typ, offset=off, vaddr=vaddr, paddr=paddr,
                                      filesz=filesz, memsz=memsz, flags=flags))
        self.symbols = []
        for s in self.sections:
            if s["type"] != self.SHT_SYMTAB:
                continue
            strsec = self.sections[s["link"]]
            for off in range(s["offset"], s["offset"] + s["size"], 16):
                name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", d, off)
                self.symbols.append(dict(name=cstr(strsec["offset"] + name), value=value, size=size,
                                         type=info & 0xF, bind=info >> 4, shndx=shndx))
        self._data = d

    def section(self, name):
        for s in self.sections:
            if s["name"] == name:
                return s
        return None

    def symbol(self, name):
        for s in self.symbols:
            if s["name"] == name:
                return s["value"]
        return None

    def read(self, addr, size):
        """Bytes at a virtual address, from the section that holds them."""
        for s in self.sections:
            if (s["type"] != self.SHT_NOBITS and s["flags"] & self.SHF_ALLOC
                    and s["addr"] <= addr and addr + size <= s["addr"] + s["size"]):
                o = s["offset"] + addr - s["addr"]
                return self._data[o:o + size]
        return None

    def functions(self):
        """{start address (Thumb bit clear): [names]} for every STT_FUNC symbol."""
        out = defaultdict(list)
        for s in self.symbols:
            if s["type"] == 2 and s["shndx"] != 0:
                out[s["value"] & ~1].append((s["name"], s["size"], s["bind"]))
        return out


# --------------------------------------------------------------------- map


_MAP_SEC1 = re.compile(r"^ (\.\S+)\s*$")
_MAP_SEC2 = re.compile(r"^ (\.\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")


def parse_map(path):
    """Linker map -> (list of linked object paths, [(start, end, object)])."""
    objects, ranges = [], []
    seen = set()
    in_memmap = False
    pending = None
    with open(path, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("LOAD "):
                o = line[5:].strip()
                if o not in seen and not o.endswith(".a"):
                    seen.add(o)
                    objects.append(o)
                continue
            if line.startswith("Linker script and memory map"):
                in_memmap = True
                continue
            if not in_memmap:
                m = re.match(r"^(\S+\.a)\((\S+)\)\s*$", line)
                if m and line not in seen:
                    seen.add(line)
                    objects.append(line.strip())
                continue
            m1 = _MAP_SEC1.match(line)
            if m1:
                pending = m1.group(1)
                continue
            m2 = _MAP_SEC2.match(line)
            if m2 and (m2.group(1) or pending):
                name = m2.group(1) or pending
                addr, size = int(m2.group(2), 16), int(m2.group(3), 16)
                obj = m2.group(4).strip()
                if size and (obj.endswith(".o") or obj.endswith(".o)") or obj.endswith(".obj")):
                    ranges.append((addr, addr + size, obj))
            pending = None
    ranges.sort()
    return objects, ranges


def ci_files_for(obj, build_dir):
    """.ci file(s) for a linked object ('x.c.o' or 'lib.a(x.c.o)'); [] if none."""
    m = re.match(r"^(.*\.a)\((.*)\)$", obj)
    if m:
        lib, member = m.group(1), m.group(2)
        libdir = os.path.dirname(os.path.join(build_dir, lib))
        stem = member[:-2] if member.endswith(".o") else member
        hits = glob.glob(os.path.join(libdir, "CMakeFiles", "*.dir", "**", stem + ".ci"), recursive=True)
        return sorted(hits)
    path = obj if os.path.isabs(obj) else os.path.join(build_dir, obj)
    if path.endswith(".o"):
        ci = path[:-2] + ".ci"
        if os.path.exists(ci):
            return [ci]
    return []


# --------------------------------------------------------------------- .ci


_CI_NODE = re.compile(r'^node: \{ title: "((?:[^"\\]|\\.)*)" label: "((?:[^"\\]|\\.)*)"')
_CI_EDGE = re.compile(r'^edge: \{ sourcename: "((?:[^"\\]|\\.)*)" targetname: "((?:[^"\\]|\\.)*)"(?: label: "((?:[^"\\]|\\.)*)")?')
_CI_SU = re.compile(r"(\d+) bytes \(([a-z,]+)\)")


class Func:
    __slots__ = ("key", "name", "frame", "qual", "callees", "indirect", "source", "loc")

    def __init__(self, key, name, frame, qual, source, loc=""):
        self.key, self.name, self.frame, self.qual = key, name, frame, qual
        self.callees = set()
        self.indirect = []        # call-site descriptions
        self.source = source      # "ci", "disasm", "missing"
        self.loc = loc


def load_ci(files):
    """Parse .ci files -> ({key: Func}, {obj_ci_path: {name: key}})."""
    funcs = {}
    per_file = {}
    for path in files:
        names = {}
        with open(path, errors="replace") as f:
            for line in f:
                m = _CI_NODE.match(line)
                if m:
                    key, label = m.group(1), m.group(2)
                    parts = label.split("\\n")
                    su = _CI_SU.search(label)
                    if not su:
                        continue      # declaration only
                    frame, qual = int(su.group(1)), su.group(2)
                    # Static functions are titled "<source file>:<name>"; the
                    # label's first line drops clone suffixes (.isra.0 etc.).
                    fm = re.match(r"^(.*\.(?:c|cc|cpp|cxx|C|S|s|h|hpp|inc)):(.+)$", key)
                    name = fm.group(2) if fm else key
                    loc = parts[1] if len(parts) > 1 else ""
                    f0 = funcs.get(key)
                    if f0 is None:
                        funcs[key] = Func(key, name, frame, qual, "ci", loc)
                    else:
                        # Same global name defined twice (static inline from a
                        # header without a TU prefix): keep the larger frame.
                        if frame > f0.frame:
                            f0.frame, f0.qual, f0.loc = frame, qual, loc
                    names[name] = key
                    continue
                m = _CI_EDGE.match(line)
                if m:
                    src, dst, label = m.group(1), m.group(2), m.group(3) or ""
                    edges = per_file.setdefault(("edges", path), [])
                    edges.append((src, dst, label))
        per_file[path] = names
    # Attach edges after all nodes exist.
    for k, v in list(per_file.items()):
        if isinstance(k, tuple):
            for src, dst, label in v:
                f = funcs.get(src)
                if f is None:
                    continue
                if dst == "__indirect_call":
                    f.indirect.append(label.rsplit("/", 1)[-1] if label else f.name)
                else:
                    f.callees.add(dst)
            del per_file[k]
    return funcs, per_file


# --------------------------------------------------------------------- disassembly


_FN_HDR = re.compile(r"^([0-9a-f]{8}) <(.+)>:$")
_INSN = re.compile(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$")
_TARGET = re.compile(r"^([0-9a-f]+) <([^>+]+)(\+0x[0-9a-f]+)?>")


def _reglist_bytes(ops):
    m = re.search(r"\{([^}]*)\}", ops)
    if not m:
        return 0
    n = 0
    for part in m.group(1).split(","):
        part = part.strip()
        r = re.match(r"^([rdsq])(\d+)\s*-\s*[rdsq]?(\d+)$", part)
        size = {"r": 4, "s": 4, "d": 8, "q": 16}
        if r:
            n += (int(r.group(3)) - int(r.group(2)) + 1) * size[r.group(1)]
        elif part:
            n += size.get(part[0], 4) if part[0] in "sdq" and part[1:].isdigit() else 4
    return n


class Disasm:
    """Per-function facts from arm-none-eabi-objdump -d."""

    def __init__(self, objdump, elf):
        self.elf = elf
        fn = elf.functions()
        self.starts = sorted(fn)
        self.names = {a: [n for n, _, _ in v] for a, v in fn.items()}
        self.size = {a: max(s for _, s, _ in v) for a, v in fn.items()}
        # Hand-written assembly often has size-0 symbols: extend to the next one.
        for i, a in enumerate(self.starts):
            if self.size[a] == 0:
                nxt = self.starts[i + 1] if i + 1 < len(self.starts) else a + 2
                self.size[a] = min(nxt - a, 4096)
        self._analysis = {}
        self.by_name = {}
        for a, v in fn.items():
            for n, _, bind in v:
                if n not in self.by_name or bind == 1:
                    self.by_name[n] = a
        out = subprocess.run([objdump, "-d", "--no-show-raw-insn", elf.path],
                             check=True, capture_output=True, text=True).stdout
        self.insns = defaultdict(list)   # function start -> [(addr, mnem, ops)]
        self.uses_fp = False
        for line in out.splitlines():
            m = _INSN.match(line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            start = self.func_at(addr)
            if start is None:
                continue
            mnem = m.group(2)
            if mnem.startswith("v") and not self.uses_fp:
                self.uses_fp = True
            self.insns[start].append((addr, mnem, m.group(3)))

    def func_at(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i < 0:
            return None
        s = self.starts[i]
        if addr < s + max(self.size[s], 2):
            return s
        return None

    def name_of(self, start):
        return self.names.get(start, [hex(start)])[0]

    def analyse(self, start):
        """(frame bytes, dynamic?, callee starts, indirect call count)."""
        if start not in self._analysis:
            self._analysis[start] = self._analyse(start)
        return self._analysis[start]

    def _analyse(self, start):
        frame, dynamic, callees, indirect = 0, False, set(), 0
        end = start + max(self.size.get(start, 0), 2)
        for addr, mnem, ops in self.insns.get(start, []):
            base = mnem.split(".")[0]
            if base == "push" or (base in ("stmdb", "stmfd") and ops.startswith("sp!")):
                frame += _reglist_bytes(ops)
            elif base in ("vpush",) or (base in ("vstmdb",) and ops.startswith("sp!")):
                frame += _reglist_bytes(ops)
            elif base in ("sub", "subw", "subs") and re.match(r"^sp,\s*(sp,\s*)?#", ops):
                frame += int(re.search(r"#(\d+)", ops).group(1))
            elif base in ("sub", "subs") and re.match(r"^sp,\s*(sp,\s*)?r", ops):
                dynamic = True
            elif base == "mov" and re.match(r"^sp,\s*r", ops):
                dynamic = True
            elif base == "msr" and re.match(r"^(msp|psp)", ops):
                dynamic = True
            elif base in ("str", "strd") and re.search(r"\[sp,\s*#-(\d+)\]!", ops):
                frame += int(re.search(r"\[sp,\s*#-(\d+)\]!", ops).group(1))
            elif base in ("bl", "blx", "b") or (base.startswith("b") and len(base) == 3 and base not in ("bic", "bfi", "bfc")):
                t = _TARGET.match(ops)
                if t:
                    ta = int(t.group(1), 16)
                    callee = self.func_at(ta)
                    if base in ("bl", "blx"):
                        if callee is not None:
                            callees.add(callee)
                    elif not (start <= ta < end) and callee == ta:
                        callees.add(callee)          # tail call
                    # A plain branch into the middle of another symbol is
                    # hand-written assembly sharing code; not a call.
                elif base in ("blx",) and re.match(r"^r\d+|^ip|^lr", ops):
                    indirect += 1
            elif base == "bx" and not ops.startswith("lr"):
                indirect += 1
            elif base == "mov" and ops.startswith("pc,"):
                indirect += 1
        return frame, dynamic, callees, indirect

    def literals(self, start):
        """Function start addresses whose Thumb address this function materialises."""
        out = set()
        movw = {}
        for addr, mnem, ops in self.insns.get(start, []):
            vals = []
            if mnem == ".word":
                vals.append(int(ops.split()[0], 16))
            elif mnem.startswith("movw"):
                m = re.match(r"^(r\d+|ip|lr),\s*#(\d+)", ops)
                if m:
                    movw[m.group(1)] = int(m.group(2))
            elif mnem.startswith("movt"):
                m = re.match(r"^(r\d+|ip|lr),\s*#(\d+)", ops)
                if m and m.group(1) in movw:
                    vals.append((int(m.group(2)) << 16) | movw.pop(m.group(1)))
            for v in vals:
                if v & 1 and (v & ~1) in self.names and (v & ~1) != start:
                    out.add(v & ~1)
        return out


# --------------------------------------------------------------------- graph


IRQ_REGISTER = {"irq_set_exclusive_handler": "exclusive", "irq_add_shared_handler": "shared"}
CALLBACK_REGISTER = {
    "alarm_pool_add_alarm_at", "alarm_pool_add_alarm_at_force_in_context",
    "alarm_pool_add_alarm_in_us", "alarm_pool_add_alarm_in_ms",
    "alarm_pool_add_repeating_timer_us", "alarm_pool_add_repeating_timer_ms",
    "add_alarm_at", "add_alarm_in_us", "add_alarm_in_ms",
    "add_repeating_timer_us", "add_repeating_timer_ms",
    "gpio_set_irq_callback", "gpio_set_irq_enabled_with_callback",
    "gpio_add_raw_irq_handler_with_order_priority_masked",
    "gpio_add_raw_irq_handler_with_order_priority_masked64",
    "gpio_add_raw_irq_handler_masked", "gpio_add_raw_irq_handler_masked64",
    "gpio_add_raw_irq_handler", "gpio_add_raw_irq_handler_with_order_priority",
    "hardware_alarm_set_callback", "timer_hardware_alarm_set_callback",
}
CORE1_LAUNCH = {"multicore_launch_core1", "multicore_launch_core1_with_stack", "multicore_launch_core1_raw"}
PRIORITY_CALLS = {"irq_set_priority", "exception_set_priority"}
SHARED_CHAIN_BYTES = 8     # irq_handler_chain.S: push {r0, lr}


def _toolchain_bin(prefix):
    exe = shutil.which(prefix + "objdump")
    if not exe:
        raise SystemExit(f"{prefix}objdump not found (install the Arm GNU toolchain)")
    return exe


class Graph:
    def __init__(self, elf_path, build_dir=None, objdump=None):
        self.elf = Elf(elf_path)
        map_path = elf_path + ".map"
        if not os.path.exists(map_path):
            alt = os.path.splitext(elf_path)[0] + ".map"
            map_path = alt if os.path.exists(alt) else map_path
        if not os.path.exists(map_path):
            raise SystemExit(f"{elf_path}: linker map not found ({map_path})")
        objects, self.ranges = parse_map(map_path)
        self.build_dir = build_dir or self._guess_build_dir(elf_path, objects)
        self.obj_ci = {}
        ci_files = []
        self.missing_ci = []
        for o in objects:
            files = ci_files_for(o, self.build_dir)
            self.obj_ci[o] = files
            ci_files += files
            if not files and not re.search(r"\.S\.o\)?$|\.s\.o\)?$", o) and "/arm-none-eabi/" not in o \
                    and "/gcc/arm-none-eabi/" not in o and not o.startswith("/usr/"):
                self.missing_ci.append(o)
        self.ci_files = sorted(set(ci_files))
        self.funcs, self.file_names = load_ci(self.ci_files)
        self.dis = Disasm(objdump or _toolchain_bin("arm-none-eabi-"), self.elf)
        self.ci_names = defaultdict(list)        # plain name -> keys
        for k, f in self.funcs.items():
            self.ci_names[f.name].append(k)
        self._addr_key = {}
        self._resolved = {}
        self._callees = {}
        self.addr_of = {}
        for a in self.dis.starts:
            self.addr_of.setdefault(self.key_for_addr(a), a)

    @staticmethod
    def _guess_build_dir(elf_path, objects):
        d = os.path.dirname(os.path.abspath(elf_path))
        probe = next((o for o in objects if not os.path.isabs(o)), None)
        while probe and d != os.path.dirname(d):
            p = re.sub(r"\(.*\)$", "", probe)
            if os.path.exists(os.path.join(d, p)):
                return d
            d = os.path.dirname(d)
        return os.path.dirname(os.path.abspath(elf_path))

    # -- name resolution ------------------------------------------------

    def _object_at(self, addr):
        i = bisect.bisect_right(self.ranges, (addr, float("inf"), "")) - 1
        if i >= 0 and self.ranges[i][0] <= addr < self.ranges[i][1]:
            return self.ranges[i][2]
        return None

    def key_for_addr(self, start):
        """The graph key of the function at an ELF address (C via .ci, else disassembly)."""
        if start in self._addr_key:
            return self._addr_key[start]
        names = self.dis.names.get(start, [])
        key = None
        obj = self._object_at(start)
        for ci in self.obj_ci.get(obj, []) if obj else []:
            table = self.file_names.get(ci, {})
            for n in names:
                if n in table:
                    key = table[n]
                    break
            if key:
                break
        if key is None:
            for n in names:
                ks = self.ci_names.get(n, [])
                if len(ks) == 1:
                    key = ks[0]
                    break
        if key is None:
            key = self._disasm_node(start)
        self._addr_key[start] = key
        return key

    def _disasm_node(self, start):
        name = self.dis.name_of(start)
        key = "asm:" + name
        if key in self.funcs:
            return key
        frame, dynamic, callees, indirect = self.dis.analyse(start)
        f = Func(key, name, frame, "dynamic" if dynamic else "estimated", "disasm")
        self.funcs[key] = f
        self._addr_key[start] = key
        for c in callees:
            f.callees.add(self.key_for_addr(c))
        f.indirect = [f"{name} (asm)"] * indirect
        return key

    def resolve(self, target):
        """A .ci edge target -> graph key (or None if nothing is linked under that name)."""
        if target in self.funcs:
            return target
        if target in self._resolved:
            return self._resolved[target]
        name = target.rsplit(":", 1)[-1] if ":" in target and "/" in target else target
        key = None
        # The Pico SDK wraps libc/libgcc entry points (-Wl,--wrap=printf etc.).
        if "__wrap_" + name in self.dis.by_name:
            key = self.key_for_addr(self.dis.by_name["__wrap_" + name])
        elif name.startswith("__real_") and name[7:] in self.dis.by_name:
            key = self.key_for_addr(self.dis.by_name[name[7:]])
        elif name in self.dis.by_name:
            key = self.key_for_addr(self.dis.by_name[name])
        elif self.ci_names.get(name):
            key = self.ci_names[name][0]
        # Otherwise nothing by that name is linked, so the call is not in the
        # final code (GCC records some libcalls, e.g. __aeabi_ldivmod, that
        # later passes remove; a real call to an undefined symbol would not link).
        self._resolved[target] = key
        return key

    def callees(self, f):
        """Calls from the .ci graph plus every direct call in the linked code."""
        if f.key in self._callees:
            return self._callees[f.key]
        if f.source == "ci":
            out = {self.resolve(c) for c in f.callees}
            out.discard(None)
            a = self.addr_of.get(f.key)
            if a is not None:
                _, _, calls, _ = self.dis.analyse(a)
                out |= {self.key_for_addr(c) for c in calls}
        else:
            out = set(f.callees)
        out = sorted(out)
        self._callees[f.key] = out
        return out

    # -- depth ----------------------------------------------------------

    def depth(self, root, indirect_targets=()):
        """Worst-case bytes from root, its path, and the unknowns reachable from it."""
        memo = {}
        onstack = []
        cycles = []
        extra = list(indirect_targets)

        def visit(k):
            if k in memo:
                return memo[k]
            if k in onstack:
                cycles.append(onstack[onstack.index(k):] + [k])
                return 0, [k]
            onstack.append(k)
            f = self.funcs[k]
            best, best_path = 0, []
            nexts = self.callees(f)
            if f.indirect and extra:
                nexts = nexts + extra
            for c in nexts:
                d, p = visit(c)
                if d > best:
                    best, best_path = d, p
            onstack.pop()
            r = (f.frame + best, [k] + best_path)
            memo[k] = r
            return r

        total, path = visit(root)
        reach = set(memo)
        unknown = {"indirect": [], "recursion": [], "dynamic": [], "no_info": [], "estimated": []}
        for k in sorted(reach):
            f = self.funcs[k]
            if f.indirect and not extra:
                unknown["indirect"].append(f"{f.name} ({len(f.indirect)} site{'s' if len(f.indirect) > 1 else ''})")
            if f.qual == "dynamic":
                unknown["dynamic"].append(f.name)
            if f.source == "missing":
                unknown["no_info"].append(f.name)
            if f.source == "disasm" and f.qual == "estimated":
                unknown["estimated"].append(f.name)
        seen = set()
        for c in cycles:
            names = " -> ".join(self.funcs[k].name for k in c)
            if names not in seen:
                seen.add(names)
                unknown["recursion"].append(names)
        return total, [(self.funcs[k].name, self.funcs[k].frame) for k in path], unknown, reach

    # -- roots ----------------------------------------------------------

    def _registrations(self, reg_names):
        """[(registration function name, caller key, [target keys])]."""
        out = []
        reg_keys = {}
        for n in reg_names:
            if n in self.dis.by_name:
                reg_keys[self.key_for_addr(self.dis.by_name[n])] = n
        if not reg_keys:
            return out
        callers = defaultdict(set)
        for k, f in list(self.funcs.items()):
            for c in self.callees(f):
                callers[c].add(k)
        for rk, rname in reg_keys.items():
            for caller in sorted(callers.get(rk, ())):
                if caller in reg_keys:
                    continue     # e.g. multicore_launch_core1 -> _with_stack: the app's call is what counts
                found = set()
                frontier, seen = [caller], set()
                for _level in range(3):          # handler passed down as an argument: look upwards
                    nxt = []
                    for c in frontier:
                        if c in seen:
                            continue
                        seen.add(c)
                        a = self.addr_of.get(c)
                        if a is not None:
                            found |= {self.key_for_addr(t) for t in self.dis.literals(a)}
                        nxt += sorted(callers.get(c, ()))
                    found.discard(rk)
                    if found:
                        break
                    frontier = nxt
                out.append((rname, caller, sorted(found)))
        return out

    def vector_handlers(self):
        """C functions the static vector table points at (exceptions and IRQs)."""
        base = self.elf.symbol("__vectors")
        out = []
        if base is None:
            return out
        n = 16 + 52     # RP2350: 52 external IRQs
        raw = self.elf.read(base, 4 * n)
        if not raw:
            return out
        for i, v in enumerate(struct.unpack("<%dI" % n, raw)):
            if i < 2 or not v & 1:
                continue
            k = self.key_for_addr(v & ~1)
            if self.funcs[k].source == "ci" and k not in out:
                out.append(k)
        return out


def analyse(elf_path, build_dir=None, objdump=None):
    g = Graph(elf_path, build_dir, objdump)
    fp = g.dis.uses_fp
    exc_frame = (104 if fp else 32) + 4    # + up to 4 bytes of 8-byte alignment padding

    main_addr = g.dis.by_name.get("main")
    if main_addr is None:
        raise SystemExit(f"{elf_path}: no main()")
    main_key = g.key_for_addr(main_addr)

    core1 = []
    core1_regs = g._registrations(CORE1_LAUNCH)
    for _reg, caller, targets in core1_regs:
        for t in targets:
            if t not in core1:
                core1.append(t)

    irq_roots = {}      # key -> kind
    for reg, caller, targets in g._registrations(IRQ_REGISTER.keys()):
        for t in targets:
            irq_roots[t] = IRQ_REGISTER[reg]
    for k in g.vector_handlers():
        irq_roots.setdefault(k, "vector")
    callbacks = []
    for _reg, caller, targets in g._registrations(CALLBACK_REGISTER):
        for t in targets:
            if t not in callbacks:
                callbacks.append(t)

    main_d, main_path, main_unknown, main_reach = g.depth(main_key)

    handlers = []
    for k, kind in irq_roots.items():
        d, path, unk, _ = g.depth(k, callbacks)
        if kind == "shared":
            d += SHARED_CHAIN_BYTES
        handlers.append(dict(name=g.funcs[k].name, kind=kind, bytes=d,
                             path=path, unknown=unk))
    for k in callbacks:
        d, path, unk, _ = g.depth(k)
        handlers.append(dict(name=g.funcs[k].name, kind="callback", bytes=d, path=path,
                             unknown=unk, note="runs inside an SDK IRQ dispatcher; counted there"))
    handlers.sort(key=lambda h: -h["bytes"])
    worst_irq = next((h for h in handlers if h["kind"] != "callback"), None)

    priority = [n for n in PRIORITY_CALLS if n in g.dis.by_name and any(
        g.key_for_addr(g.dis.by_name[n]) in g.callees(g.funcs[k]) for k in main_reach)]

    core1_out = []
    for k in core1:
        d, path, unk, _ = g.depth(k)
        core1_out.append(dict(name=g.funcs[k].name, bytes=d, path=path, unknown=unk))

    combined = main_d + (exc_frame + worst_irq["bytes"] if worst_irq else 0)
    return dict(
        elf=elf_path,
        fpu=fp,
        exception_frame=exc_frame,
        main=dict(bytes=main_d, path=main_path, unknown=main_unknown),
        irq=handlers,
        worst_irq=worst_irq["name"] if worst_irq else None,
        worst_irq_bytes=worst_irq["bytes"] if worst_irq else 0,
        combined=combined,
        irq_priorities_changed=sorted(priority),
        core1=core1_out,
        core1_launch_sites=len(core1_regs),
        objects_without_ci=g.missing_ci,
        ci_files=len(g.ci_files),
    )


def count_unknown(unk, include_estimated=False):
    keys = ["indirect", "recursion", "dynamic", "no_info"] + (["estimated"] if include_estimated else [])
    return sum(len(unk.get(k, [])) for k in keys)


def format_path(path, limit=12):
    parts = [f"{n} ({b})" for n, b in path]
    if len(parts) > limit:
        parts = parts[:limit - 1] + [f"... {len(path) - limit + 1} more"]
    return " > ".join(parts)


def format_unknown(unk, indent="    ", limit=8):
    lines = []
    labels = [("indirect", "indirect calls (targets not followed)"),
              ("recursion", "recursion (depth unbounded)"),
              ("dynamic", "dynamic stack (alloca/VLA or SP moved)"),
              ("no_info", "no stack information"),
              ("estimated", "frames estimated from disassembly (asm / prebuilt libc)")]
    for k, label in labels:
        v = unk.get(k) or []
        if v:
            shown = ", ".join(v[:limit]) + (f", ... {len(v) - limit} more" if len(v) > limit else "")
            lines.append(f"{indent}{label}: {shown}")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("elf", nargs="+")
    ap.add_argument("--build-dir", help="hwcheck build directory (default: found from the map)")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    results = [analyse(e, a.build_dir) for e in a.elf]
    if a.json:
        json.dump(results, sys.stdout, indent=2)
        print()
        return
    for r in results:
        print(r["elf"])
        print(f"  main        {r['main']['bytes']:6d} B   {format_path(r['main']['path'])}")
        print("\n".join(format_unknown(r["main"]["unknown"])))
        for h in r["irq"]:
            print(f"  {h['kind']:<11} {h['bytes']:6d} B   {format_path(h['path'])}")
            print("\n".join(format_unknown(h["unknown"])))
        print(f"  combined    {r['combined']:6d} B   main + {r['exception_frame']} B exception entry"
              f" + {r['worst_irq'] or 'no IRQ'}")
        for c in r["core1"]:
            print(f"  core1       {c['bytes']:6d} B   {format_path(c['path'])}")


if __name__ == "__main__":
    main()
