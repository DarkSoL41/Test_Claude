#!/usr/bin/env python3
"""Write the annotated disassembly of the Supaplex program files.

Uses the same code discovery, routine names, variable names and comments as the
translator (tools/translate.json), so the listing and the C++ port share one
vocabulary. Output: Motorola syntax listing per program file.

usage: m68kdis.py <config.json> <out-dir>
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import m68k2cpp  # noqa: E402
from m68kdec import CC  # noqa: E402


def h(v):
    return '$%X' % v if v >= 10 or v < 0 else str(v)


def sh(v):
    return ('-$%X' % -v) if v < 0 else ('$%X' % v)


class Lister:
    def __init__(self, prog):
        self.p = prog
        self.fnames = {}
        for e in prog.funcs:
            self.fnames[e] = prog.names.get(e, 'sub_%06X' % e)
        for a, n in prog.natives.items():
            self.fnames[a] = prog.names.get(a, 'sub_%06X' % a)

    def lab(self, a):
        if a in self.fnames:
            return self.fnames[a]
        if a in self.p.symbols:
            return self.p.symbols[a]
        return 'L_%06X' % a

    def ea(self, o, sz):
        k = o.kind
        if k == 'dn': return 'd%d' % o.reg
        if k == 'an': return 'a%d' % o.reg if o.reg != 7 else 'sp'
        if k == 'ind': return '(a%d)' % o.reg
        if k == 'post': return '(a%d)+' % o.reg
        if k == 'pre': return '-(a%d)' % o.reg
        if k == 'disp': return '%s(a%d)' % (sh(o.disp), o.reg)
        if k == 'idx':
            x = ('d%d' % o.xreg) if o.xreg < 8 else ('a%d' % (o.xreg - 8))
            return '%s(a%d,%s.%s)' % (sh(o.disp), o.reg, x, 'l' if o.xlong else 'w')
        if k == 'absw': return '%s.w' % self.addr(o.addr)
        if k == 'absl': return self.addr(o.addr)
        if k == 'pcdisp': return '%s(pc)' % self.addr(o.addr)
        if k == 'pcidx':
            x = ('d%d' % o.xreg) if o.xreg < 8 else ('a%d' % (o.xreg - 8))
            return '%s(pc,%s.%s)' % (self.addr(o.addr), x, 'l' if o.xlong else 'w')
        if k == 'imm':
            if sz == 'l' and (o.val in self.fnames or o.val in self.p.code_consts):
                return '#' + self.lab(o.val)
            if sz == 'l' and o.val in self.p.symbols:
                return '#' + self.p.symbols[o.val]
            v = o.val
            return '#%s' % h(v)
        if k == 'reglist': return self.reglist(o.val)
        return k

    def addr(self, a):
        if a in self.fnames or a in self.p.code or a in self.p.symbols:
            if a in self.fnames or a in self.p.symbols or a in self.labels:
                return self.lab(a)
        return '$%X' % a

    @staticmethod
    def reglist(mask, rev=False):
        regs = []
        for i in range(16):
            if mask & (1 << i):
                regs.append(('d%d' % i) if i < 8 else ('a%d' % (i - 8)))
        return '/'.join(regs)

    def insn_text(self, ins):
        mn = ins.mn
        if ins.sz and mn not in ('moveq', 'lea', 'pea', 'swap', 'exg') and not (mn.startswith('db') or mn in ('bra', 'bsr') or (ins.cc is not None and mn.startswith('b'))):
            mn = mn + '.' + ins.sz
        ops = []
        for o in ins.ops:
            if o.kind == 'reglist':
                mask = o.val
                other = [x for x in ins.ops if x.kind != 'reglist'][0]
                if other.kind == 'pre':
                    mask = int('{:016b}'.format(mask)[::-1], 2)
                ops.append(self.reglist(mask))
            elif o.kind in ('sr', 'ccr', 'usp'):
                ops.append(o.kind)
            else:
                ops.append(self.ea(o, ins.sz))
        if ins.target is not None and mn.split('.')[0] not in ('jsr', 'jmp'):
            ops.append(self.lab(ins.target))
        elif ins.target is not None and not ops:
            ops.append(self.lab(ins.target))
        return mn, ', '.join(ops)

    def listing(self, image, out):
        p = self.p
        self.labels = set()
        for a in p.code:
            ins = p.insns[a]
            if ins.target is not None:
                self.labels.add(ins.target)
        self.labels |= set(p.code_consts)
        comments = {int(k, 16): v for k, v in p.cfg.get('comments', {}).items()}
        line_comments = {int(k, 16): v for k, v in p.cfg.get('line_comments', {}).items()}
        a = image.base
        end = image.end
        data_run = []

        def flush_data():
            if not data_run:
                return
            start = data_run[0]
            chunk = image.data[start - image.base:data_run[-1] + 1 - image.base]
            for i in range(0, len(chunk), 16):
                c = chunk[i:i + 16]
                txt = ''.join(chr(x) if 32 <= x < 127 else '.' for x in c)
                out.write('        dc.b    %-64s ; %06X %s\n' % (','.join('$%02X' % x for x in c), start + i, txt))
            data_run.clear()

        while a < end:
            if a in p.code:
                flush_data()
                ins = p.insns[a]
                if a in self.fnames:
                    out.write('\n; ' + '-' * 76 + '\n')
                    if a in comments:
                        for cl in comments[a].split('\n'):
                            out.write('; %s\n' % cl)
                    out.write('%s:\n' % self.fnames[a])
                elif a in self.labels or a in p.symbols:
                    out.write('%s:\n' % self.lab(a))
                mn, ops = self.insn_text(ins)
                raw = image.data[a - image.base:a - image.base + ins.size].hex()
                lc = line_comments.get(a, '')
                out.write('        %-8s%-34s ; %06X %-20s %s\n' % (mn, ops, a, raw, lc))
                a += ins.size
            else:
                if a in p.symbols:
                    flush_data()
                    out.write('%s:\n' % p.symbols[a])
                data_run.append(a)
                a += 1
                if len(data_run) >= 4096:
                    flush_data()
        flush_data()


def main():
    cfg_path, out_dir = sys.argv[1], sys.argv[2]
    cfg = json.load(open(cfg_path))
    base_dir = os.path.dirname(os.path.abspath(cfg_path))
    cfg['_base_dir'] = base_dir
    if not os.path.isabs(cfg['adf']):
        cfg['adf'] = os.path.join(base_dir, cfg['adf'])
    p = m68k2cpp.Program(cfg)
    p.code_ranges = [(int(x, 16), int(y, 16)) for x, y in cfg['code_ranges']]
    for t in cfg.get('tables', []):
        ta = int(t['addr'], 16)
        for i in range(t['count']):
            v = p.image_of(ta).l(ta + i * t.get('stride', 4) + t.get('offset', 0))
            if v:
                p.entries.add(v)
                p.code_consts.add(v)
    p.discover()
    p.build_functions()
    lst = Lister(p)
    os.makedirs(out_dir, exist_ok=True)
    for im in p.images:
        with open(os.path.join(out_dir, '%s.asm' % im.name), 'w') as f:
            f.write('; Supaplex (Amiga, 1991) — disassembly of %s, loaded at $%06X (%d bytes)\n' % (
                im.name, im.base, len(im.data)))
            f.write('; generated by tools/m68kdis.py from tools/translate.json (names and comments)\n')
            f.write('        org     $%X\n' % im.base)
            lst.listing(im, f)
        print('%s.asm written' % im.name)


if __name__ == '__main__':
    main()
