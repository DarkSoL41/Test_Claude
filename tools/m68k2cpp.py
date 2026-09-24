#!/usr/bin/env python3
"""Translate the original Supaplex 68000 code into C++ (first-draft of the port).

Every 68000 routine becomes one C++ function operating on the global register
file (`game::cpu`) and on the emulated Amiga memory, so the behaviour is
identical to the original, including edge cases such as reads outside the
level map. Each generated line carries the original address and instruction.

usage: m68k2cpp.py <config.json>
"""
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from m68kdec import Decoder, Illegal, CC  # noqa: E402
from adflib import Adf  # noqa: E402

SZN = {'b': 1, 'w': 2, 'l': 4}
FLAG_SETTERS_LOGIC = {'move', 'moveq', 'tst', 'clr', 'not', 'and', 'andi', 'or', 'ori', 'eor', 'eori',
                      'swap', 'ext', 'mulu', 'muls', 'divu', 'divs'}
ARITH = {'add', 'addi', 'addq', 'sub', 'subi', 'subq', 'neg', 'cmp', 'cmpi', 'cmpa', 'cmpm'}
SHIFTS = {'lsl', 'lsr', 'asl', 'asr', 'rol', 'ror', 'roxl', 'roxr'}
BITOPS = {'btst', 'bset', 'bclr', 'bchg'}


class Image:
    def __init__(self, name, data, base):
        self.name = name
        self.data = data
        self.base = base
        self.end = base + len(self.data)
        self.dec = Decoder(self.data, base)

    def contains(self, a):
        return self.base <= a < self.end

    def l(self, a):
        o = a - self.base
        return struct.unpack('>I', self.data[o:o + 4])[0]


class Program:
    def __init__(self, cfg):
        self.cfg = cfg
        adf = Adf(cfg['adf'])
        self.images = [Image(i['name'], adf.read(i['name']), int(i['base'], 16)) for i in cfg['images']]
        self.insns = {}          # addr -> Insn
        self.entries = set(int(x, 16) for x in cfg['entries'])
        self.natives = {int(k, 16): v for k, v in cfg.get('natives', {}).items()}
        self.names = {int(k, 16): v for k, v in cfg.get('names', {}).items()}
        self.symbols = {}
        for k, v in cfg.get('symbols', {}).items():
            self.symbols[int(k, 16)] = v
        self.exclude = set(int(x, 16) for x in cfg.get('exclude', []))
        self.code_consts = set()

    def image_of(self, a):
        for im in self.images:
            if im.contains(a):
                return im
        return None

    def decode(self, a):
        if a in self.insns:
            return self.insns[a]
        im = self.image_of(a)
        if im is None:
            return None
        ins = im.dec.decode(a)
        self.insns[a] = ins
        return ins

    # ---- discovery ---------------------------------------------------------
    def discover(self):
        todo = list(self.entries)
        seen = set()
        while todo:
            a = todo.pop()
            while True:
                if a in seen or a in self.natives or a in self.exclude:
                    break
                im = self.image_of(a)
                if im is None or a & 1:
                    break
                try:
                    ins = self.decode(a)
                except Illegal as e:
                    print('warning: illegal instruction at %06X (%s)' % (a, e), file=sys.stderr)
                    break
                seen.add(a)
                mn = ins.mn
                if ins.target is not None:
                    t = ins.target
                    if mn in ('bsr', 'jsr'):
                        self.entries.add(t)
                        todo.append(t)
                    elif mn == 'jmp':
                        todo.append(t)
                    else:
                        todo.append(t)
                # code address constants (lea / move #imm / pea)
                for o in ins.ops:
                    if o.kind in ('absl', 'pcdisp') and mn in ('lea', 'pea'):
                        if self.in_code_range(o.addr) and o.addr not in self.exclude:
                            self.code_consts.add(o.addr)
                            self.entries.add(o.addr)
                            todo.append(o.addr)
                    dst = ins.ops[1] if len(ins.ops) > 1 else None
                    is_vec = dst is not None and (dst.kind == 'an' or (dst.kind in ('absw', 'absl') and dst.addr < 0x400))
                    if o.kind == 'imm' and mn in ('move', 'movea') and ins.sz == 'l' and is_vec:
                        if self.in_code_range(o.val) and o.val not in self.exclude:
                            self.code_consts.add(o.val)
                            self.entries.add(o.val)
                            todo.append(o.val)
                a += ins.size
                if mn in ('rts', 'rte', 'rtr', 'bra', 'jmp', 'illegal') or mn == 'stop':
                    break
        self.code = seen

    def in_code_range(self, a):
        return any(lo <= a < hi for lo, hi in self.code_ranges) and not (a & 1)

    def cfg_code_consts(self):
        return self

    # ---- functions -----------------------------------------------------------
    def build_functions(self):
        self.funcs = {}
        for e in sorted(self.entries):
            if e in self.natives or e not in self.code:
                continue
            body = set()
            tails = set()
            todo = [e]
            while todo:
                a = todo.pop()
                if a in body:
                    continue
                if a != e and a in self.entries:
                    tails.add(a)
                    continue
                if a not in self.code:
                    continue
                body.add(a)
                ins = self.insns[a]
                mn = ins.mn
                nxt = a + ins.size
                if mn in ('rts', 'rte', 'rtr', 'illegal'):
                    continue
                if mn == 'bra':
                    todo.append(ins.target)
                    continue
                if mn == 'jmp':
                    if ins.target is not None:
                        todo.append(ins.target)
                    else:
                        # computed goto: may reach any code constant inside this function
                        pass
                    continue
                if ins.target is not None and mn not in ('bsr', 'jsr'):
                    todo.append(ins.target)
                todo.append(nxt)
            self.funcs[e] = (body, tails)
        # computed gotos: add code constants that are labels in the function
        for e, (body, tails) in self.funcs.items():
            pass

    # ---- flag liveness -------------------------------------------------------
    def flag_uses(self, ins):
        mn = ins.mn
        if mn.startswith('b') and ins.cc is not None and mn not in ('bra', 'bsr'):
            return True
        if mn.startswith('db') and ins.cc not in (0, 1):
            return True
        if mn.startswith('s') and ins.cc is not None and ins.cc not in (0, 1):
            return True
        if mn in ('roxl', 'roxr', 'addx', 'subx', 'negx'):
            return True
        if mn == 'move' and ins.ops and ins.ops[0].kind in ('sr', 'ccr'):
            return True
        if mn in ('ori', 'andi', 'eori') and ins.ops[1].kind in ('ccr', 'sr'):
            return True
        return False

    def flag_defs(self, ins):
        mn = ins.mn
        if mn in ('movea', 'lea', 'pea', 'exg', 'adda', 'suba', 'bra', 'bsr', 'jsr', 'jmp', 'rts', 'nop',
                  'movem', 'link', 'unlk', 'trap'):
            return False
        if mn in ('addq', 'subq') and ins.ops[1].kind == 'an':
            return False
        if mn.startswith('db') or (mn.startswith('b') and ins.cc is not None) or \
                (mn.startswith('s') and ins.cc is not None):
            return False
        if mn == 'move' and ins.ops[1].kind in ('sr', 'ccr', 'usp'):
            return True
        if mn in ('ori', 'andi', 'eori') and ins.ops[1].kind in ('ccr', 'sr'):
            return True
        return True

    def liveness(self):
        """Per instruction: are the flags it produces needed afterwards?"""
        # per function: entry_uses (flags read before written), exit_live (callers need flags)
        self.exit_live = {e: False for e in self.funcs}
        self.entry_uses = {e: False for e in self.funcs}
        self.live_out = {}
        changed = True
        it = 0
        while changed and it < 50:
            changed = False
            it += 1
            for e, (body, tails) in self.funcs.items():
                lo = self.func_liveness(e, body)
                for k, v in lo.items():
                    key = (e, k)
                    if self.live_out.get(key) != v:
                        self.live_out[key] = v
                # entry use
                ent = self._live_in.get(e, False)
                if ent != self.entry_uses[e]:
                    self.entry_uses[e] = ent
                    changed = True
                # call sites: if flags live after call -> callee exit_live
                for a in body:
                    ins = self.insns[a]
                    if ins.mn in ('bsr', 'jsr'):
                        t = ins.target
                        if t in self.funcs and self.live_out[(e, a)] and not self.exit_live[t]:
                            self.exit_live[t] = True
                            changed = True
                # tail calls inherit exit liveness
                for t in tails:
                    if t in self.funcs and self.exit_live[e] and not self.exit_live[t]:
                        self.exit_live[t] = True
                        changed = True

    def func_liveness(self, e, body):
        # backward dataflow over the function's instructions; returns live_out per insn
        order = sorted(body)
        succ = {}
        for a in order:
            ins = self.insns[a]
            mn = ins.mn
            s = []
            nxt = a + ins.size
            if mn in ('rts', 'rte', 'rtr'):
                s = ['EXIT']
            elif mn == 'bra':
                s = [ins.target]
            elif mn == 'jmp':
                s = [ins.target] if ins.target is not None else ['EXIT']
            elif ins.target is not None and mn not in ('bsr', 'jsr'):
                s = [ins.target, nxt]
            else:
                s = [nxt]
            succ[a] = s
        live_in = {a: False for a in order}
        changed = True
        while changed:
            changed = False
            for a in reversed(order):
                ins = self.insns[a]
                out = False
                for t in succ[a]:
                    if t == 'EXIT':
                        out = out or self.exit_live[e]
                    elif t in body:
                        out = out or live_in[t]
                    elif t in self.funcs:  # tail call into another function
                        out = out or self.entry_uses.get(t, False)
                    else:
                        out = True
                if ins.mn in ('bsr', 'jsr'):
                    t = ins.target
                    # callee defines flags on return; before the call flags live if callee reads them
                    callee_uses = self.entry_uses.get(t, True) if t is not None else True
                    li = callee_uses
                    self._lo_tmp = out
                else:
                    if self.flag_uses(ins):
                        li = True
                    elif self.flag_defs(ins):
                        li = False
                    else:
                        li = out
                if li != live_in[a]:
                    live_in[a] = li
                    changed = True
        # compute live_out
        lo = {}
        for a in order:
            ins = self.insns[a]
            out = False
            for t in succ[a]:
                if t == 'EXIT':
                    out = out or self.exit_live[e]
                elif t in body:
                    out = out or live_in[t]
                elif t in self.funcs:
                    out = out or self.entry_uses.get(t, False)
                else:
                    out = True
            lo[a] = out
        if not hasattr(self, '_live_in'):
            self._live_in = {}
        self._live_in[e] = live_in.get(e, False)
        return lo


# ---------------------------------------------------------------------------
# C++ emission

def hx(v, w=0):
    return ('0x%0' + str(w) + 'X') % v if w else '0x%X' % v


REG = {**{('dn', i): 'D%d' % i for i in range(8)}, **{('an', i): 'A%d' % i for i in range(8)}}


def xreg(x):
    return ('D%d' % x) if x < 8 else ('A%d' % (x - 8))


class Emitter:
    def __init__(self, prog, trace=False):
        self.p = prog
        self.trace = trace

    def sym(self, addr):
        s = self.p.symbols.get(addr)
        if s:
            return s
        return hx(addr, 5)

    def addr_expr(self, o, sz, pre, post):
        """Address expression for memory operand o. pre/post collect side-effect statements."""
        k = o.kind
        n = SZN[sz] if sz else 2
        if k == 'ind':
            return 'A%d' % o.reg
        if k == 'post':
            step = 2 if (o.reg == 7 and n == 1) else n
            post.append('A%d += %d;' % (o.reg, step))
            return 'A%d' % o.reg
        if k == 'pre':
            step = 2 if (o.reg == 7 and n == 1) else n
            pre.append('A%d -= %d;' % (o.reg, step))
            return 'A%d' % o.reg
        if k == 'disp':
            if o.disp == 0:
                return 'A%d' % o.reg
            return 'A%d %s 0x%X' % (o.reg, '-' if o.disp < 0 else '+', abs(o.disp))
        if k == 'idx':
            x = xreg(o.xreg)
            xs = x if o.xlong else 'sxw(%s)' % x
            d = '' if o.disp == 0 else (' %s 0x%X' % ('-' if o.disp < 0 else '+', abs(o.disp)))
            return 'A%d + %s%s' % (o.reg, xs, d)
        if k in ('absw', 'absl', 'pcdisp'):
            return self.sym(o.addr)
        if k == 'pcidx':
            x = xreg(o.xreg)
            xs = x if o.xlong else 'sxw(%s)' % x
            return '0x%X + %s' % (o.addr, xs)
        raise ValueError('addr of %s' % k)

    def read(self, o, sz, pre, post, signext_word_an=False):
        k = o.kind
        if k == 'dn':
            r = 'D%d' % o.reg
            return {'b': '(%s & 0xFF)' % r, 'w': '(%s & 0xFFFF)' % r, 'l': r}[sz]
        if k == 'an':
            r = 'A%d' % o.reg
            return {'b': '(%s & 0xFF)' % r, 'w': '(%s & 0xFFFF)' % r, 'l': r}[sz]
        if k == 'imm':
            v = o.val & {'b': 0xFF, 'w': 0xFFFF, 'l': 0xFFFFFFFF}[sz]
            return hx(v)
        a = self.addr_expr(o, sz, pre, post)
        return 'rd%d(%s)' % (SZN[sz] * 8, a)

    def write(self, o, sz, val, pre, post, addr=None):
        k = o.kind
        if k == 'dn':
            r = 'D%d' % o.reg
            return {'b': 'setB(%s, %s);', 'w': 'setW(%s, %s);', 'l': '%s = %s;'}[sz] % (r, val)
        if k == 'an':
            return 'A%d = %s;' % (o.reg, val)
        a = addr if addr is not None else self.addr_expr(o, sz, pre, post)
        return 'wr%d(%s, %s);' % (SZN[sz] * 8, a, val)

    def rmw(self, o, sz, fn):
        """Read-modify-write on operand o. fn(valexpr) -> newvalexpr (may contain flag helper)."""
        pre, post = [], []
        k = o.kind
        if k in ('dn', 'an'):
            v = self.read(o, sz, pre, post)
            return [self.write(o, sz, fn(v), pre, post)]
        a = self.addr_expr(o, sz, pre, post)
        if pre or post or k == 'idx':
            lines = pre + ['uint32_t ea = %s;' % a]
            lines.append(self.write(o, sz, fn('rd%d(ea)' % (SZN[sz] * 8)), [], [], addr='ea'))
            lines += post
            return ['{ ' + ' '.join(lines) + ' }']
        return [self.write(o, sz, fn('rd%d(%s)' % (SZN[sz] * 8, a)), [], [], addr=a)]

    def target_code(self, fe, t, body):
        if t in body:
            return 'goto L_%06X;' % t
        return '{ %s(); return; }' % self.fname(t)

    def fname(self, a):
        if a in self.p.natives:
            return self.p.natives[a]
        n = self.p.names.get(a)
        return n if n else 'sub_%06X' % a

    def emit_insn(self, fe, body, ins, live):
        """Return list of C++ lines for instruction."""
        mn = ins.mn
        sz = ins.sz
        ops = ins.ops
        L = []
        n = SZN.get(sz, 0)
        S = str(n)

        def flags_logic(expr):
            return 'logic<%s>(%s)' % (S, expr)

        if mn in ('move', 'movea'):
            src, dst = ops
            if dst.kind == 'ccr':
                pre, post = [], []
                return pre + ['setCCR(%s);' % self.read(src, 'w', pre, post)] + post
            if dst.kind == 'sr':
                pre, post = [], []
                return pre + ['setSR(%s);' % self.read(src, 'w', pre, post)] + post
            if src.kind == 'sr':
                pre, post = [], []
                return [self.write(dst, 'w', 'getSR()', pre, post)] + post if not pre else pre + [self.write(dst, 'w', 'getSR()', [], post)] + post
            if src.kind == 'usp' or dst.kind == 'usp':
                return ['/* move usp ignored */']
            pre, post = [], []
            v = self.read(src, sz, pre, post)
            if mn == 'movea':
                if sz == 'w':
                    v = 'sxw(%s)' % v
                return pre + post + ['A%d = %s;' % (dst.reg, v)] if not post else \
                    ['{ uint32_t v = %s; %s A%d = v; }' % (v, ' '.join(post), dst.reg)] if not pre else \
                    ['{ %s uint32_t v = %s; %s A%d = v; }' % (' '.join(pre), v, ' '.join(post), dst.reg)]
            if live:
                v = flags_logic(v)
            dpre, dpost = [], []
            if pre or post:
                w = self.write(dst, sz, 'v', dpre, dpost)
                return ['{ %s uint32_t v = %s; %s %s %s %s }' % (' '.join(pre), v, ' '.join(post),
                                                               ' '.join(dpre), w, ' '.join(dpost))]
            w = self.write(dst, sz, v, dpre, dpost)
            if dpre or dpost:
                return ['{ %s %s %s }' % (' '.join(dpre), w, ' '.join(dpost))]
            return [w]
        if mn == 'moveq':
            v = hx(ops[0].val & 0xFFFFFFFF)
            if live:
                v = 'logic<4>(%s)' % v
            return ['D%d = %s;' % (ops[1].reg, v)]
        if mn == 'lea':
            pre, post = [], []
            a = self.addr_expr(ops[0], 'l', pre, post)
            return ['A%d = %s;' % (ops[1].reg, a)]
        if mn == 'pea':
            pre, post = [], []
            a = self.addr_expr(ops[0], 'l', pre, post)
            return ['push32(%s);' % a]
        if mn == 'clr':
            pre, post = [], []
            L = [self.write(ops[0], sz, '0', pre, post)]
            L = pre + L + post
            if live:
                L.append('logic<%s>(0);' % S)
            return L
        if mn == 'tst':
            pre, post = [], []
            v = self.read(ops[0], sz, pre, post)
            return pre + ['logic<%s>(%s);' % (S, v)] + post if live else pre + (['(void)%s;' % v] if 'rd' in v else []) + post
        if mn in ('cmp', 'cmpi', 'cmpa', 'cmpm'):
            src, dst = ops
            pre, post = [], []
            s = self.read(src, sz, pre, post)
            if mn == 'cmpa':
                d = 'A%d' % dst.reg
                if sz == 'w':
                    s = 'sxw(%s)' % s
                szz = '4'
            else:
                d = self.read(dst, sz, pre, post)
                szz = S
            if pre or post:
                return ['{ %s uint32_t s = %s; %s uint32_t d = %s; %s cmp<%s>(s, d); }' % (
                    '', s, '', d, ' '.join(post), szz)] if not pre else \
                    ['{ %s uint32_t s = %s; uint32_t d = %s; %s cmp<%s>(s, d); }' % (' '.join(pre), s, d, ' '.join(post), szz)]
            return ['cmp<%s>(%s, %s);' % (szz, s, d)]
        if mn in ('add', 'addi', 'addq', 'sub', 'subi', 'subq', 'adda', 'suba'):
            src, dst = ops
            isadd = mn.startswith('add')
            if mn in ('adda', 'suba') or dst.kind == 'an':
                pre, post = [], []
                s = self.read(src, sz, pre, post)
                if sz == 'w' and mn in ('adda', 'suba'):
                    s = 'sxw(%s)' % s
                op = '+=' if isadd else '-='
                line = 'A%d %s %s;' % (dst.reg, op, s)
                if pre or post:
                    return ['{ %s uint32_t s = %s; %s A%d %s s; }' % (' '.join(pre), s, ' '.join(post), dst.reg, op)]
                return [line]
            pre, post = [], []
            s = self.read(src, sz, pre, post)
            fn = 'add' if isadd else 'sub'
            if pre or post:
                # source with side effects: evaluate first
                inner = self.rmw(dst, sz, lambda v: ('%s<%s>(s, %s)' % (fn, S, v)) if live else
                                 ('(%s %s s)' % (v, '+' if isadd else '-')))
                return ['{ %s uint32_t s = %s; %s %s }' % (' '.join(pre), s, ' '.join(post), ' '.join(inner))]
            return self.rmw(dst, sz, lambda v: ('%s<%s>(%s, %s)' % (fn, S, s, v)) if live else
                            ('(%s %s %s)' % (v, '+' if isadd else '-', s)))
        if mn in ('and', 'andi', 'or', 'ori', 'eor', 'eori'):
            src, dst = ops
            if dst.kind in ('ccr', 'sr'):
                op = {'and': '&', 'or': '|', 'eor': '^'}[mn.rstrip('i')]
                if dst.kind == 'ccr':
                    return ['setCCR(getCCR() %s 0x%X);' % (op, src.val)]
                return ['setSR(getSR() %s 0x%X);' % (op, src.val)]
            op = {'and': '&', 'or': '|', 'eor': '^'}[mn.rstrip('i')]
            pre, post = [], []
            s = self.read(src, sz, pre, post)
            f = (lambda v: 'logic<%s>(%s %s %s)' % (S, v, op, s)) if live else (lambda v: '(%s %s %s)' % (v, op, s))
            if pre or post:
                inner = self.rmw(dst, sz, (lambda v: 'logic<%s>(%s %s s)' % (S, v, op)) if live else (lambda v: '(%s %s s)' % (v, op)))
                return ['{ %s uint32_t s = %s; %s %s }' % (' '.join(pre), s, ' '.join(post), ' '.join(inner))]
            return self.rmw(dst, sz, f)
        if mn == 'not':
            return self.rmw(ops[0], sz, (lambda v: 'logic<%s>(~%s)' % (S, v)) if live else (lambda v: '(~%s)' % v))
        if mn == 'neg':
            return self.rmw(ops[0], sz, (lambda v: 'neg<%s>(%s)' % (S, v)) if live else (lambda v: '(0 - %s)' % v))
        if mn == 'ext':
            r = 'D%d' % ops[0].reg
            if sz == 'w':
                v = '(sxb(%s) & 0xFFFF)' % r
                return ['setW(%s, %s);' % (r, 'logic<2>(%s)' % v if live else v)]
            v = 'sxw(%s)' % r
            return ['%s = %s;' % (r, 'logic<4>(%s)' % v if live else v)]
        if mn == 'swap':
            r = 'D%d' % ops[0].reg
            v = '((%s << 16) | (%s >> 16))' % (r, r)
            return ['%s = %s;' % (r, 'logic<4>(%s)' % v if live else v)]
        if mn == 'exg':
            a, b = ops
            ra = REG[(a.kind, a.reg)]
            rb = REG[(b.kind, b.reg)]
            return ['{ uint32_t t = %s; %s = %s; %s = t; }' % (ra, ra, rb, rb)]
        if mn in SHIFTS:
            if len(ops) == 1:  # memory shift by 1 (word)
                return self.rmw(ops[0], 'w', lambda v: '%s<2>(%s, 1)' % (mn, v))
            cnt, dst = ops
            if cnt.kind == 'imm':
                c = str(cnt.val)
            else:
                c = '(D%d & 63)' % cnt.reg
            r = 'D%d' % dst.reg
            v = self.read(dst, sz, [], [])
            if live or mn in ('roxl', 'roxr'):
                ex = '%s<%s>(%s, %s)' % (mn, S, v, c)
            else:
                # plain C for dead flags
                bits = n * 8
                m = {1: '0xFF', 2: '0xFFFF', 4: '0xFFFFFFFFu'}[n]
                if cnt.kind == 'imm':
                    k = cnt.val
                    if mn == 'lsl':
                        ex = '((%s << %d) & %s)' % (v, k, m) if k < bits else '0'
                    elif mn == 'lsr':
                        ex = '(%s >> %d)' % (v, k) if k < bits else '0'
                    else:
                        ex = '%s<%s>(%s, %s)' % (mn, S, v, c)
                else:
                    ex = '%s<%s>(%s, %s)' % (mn, S, v, c)
            return [self.write(dst, sz, ex, [], [])]
        if mn in BITOPS:
            bit, dst = ops
            pre, post = [], []
            if bit.kind == 'imm':
                b = str(bit.val & (31 if dst.kind == 'dn' else 7))
            else:
                b = '(D%d & %d)' % (bit.reg, 31 if dst.kind == 'dn' else 7)
            szz = 'l' if dst.kind == 'dn' else 'b'
            if mn == 'btst':
                v = self.read(dst, szz, pre, post)
                return pre + ['btst(%s, %s);' % (v, b)] + post
            op = {'bset': '| (1u << %s)', 'bclr': '& ~(1u << %s)', 'bchg': '^ (1u << %s)'}[mn] % b
            if dst.kind == 'dn':
                r = 'D%d' % dst.reg
                return ['btst(%s, %s); %s = %s %s;' % (r, b, r, r, op)]
            a = self.addr_expr(dst, 'b', pre, post)
            if pre or post:
                return ['{ %s uint32_t ea = %s; uint32_t v = rd8(ea); btst(v, %s); wr8(ea, v %s); %s }' % (
                    ' '.join(pre), a, b, op, ' '.join(post))]
            return ['{ uint32_t v = rd8(%s); btst(v, %s); wr8(%s, v %s); }' % (a, b, a, op)]
        if mn in ('mulu', 'muls', 'divu', 'divs'):
            src, dst = ops
            pre, post = [], []
            s = self.read(src, 'w', pre, post)
            r = 'D%d' % dst.reg
            return pre + post + ['%s = %s(%s, %s);' % (r, mn, s, r)]
        if mn.startswith('s') and ins.cc is not None and mn not in ('sub', 'subq', 'subi', 'suba', 'swap', 'subx'):
            cond = 'CC_' + CC[ins.cc].upper()
            pre, post = [], []
            return self.rmw_simple(ops[0], 'b', '(%s ? 0xFF : 0x00)' % cond)
        if mn == 'movem':
            return self.emit_movem(ins)
        if mn == 'nop':
            return []
        if mn == 'bra':
            return [self.target_code(fe, ins.target, body)]
        if mn == 'bsr' or (mn == 'jsr' and ins.target is not None):
            return ['push32(0x%X); %s(); A7 += 4;' % (ins.addr + ins.size, self.fname(ins.target))]
        if mn == 'jsr':
            pre, post = [], []
            a = self.addr_expr(ops[0], 'l', pre, post)
            return ['push32(0x%X); callAddress(%s); A7 += 4;' % (ins.addr + ins.size, a)]
        if mn == 'jmp':
            if ins.target is not None:
                return [self.target_code(fe, ins.target, body)]
            pre, post = [], []
            a = self.addr_expr(ops[0], 'l', pre, post)
            # computed goto: labels of this function first, else tail call
            cases = sorted(c for c in self.p.code_consts if c in body)
            if cases:
                sw = 'switch (%s) { %s default: callAddress(%s); return; }' % (
                    a, ' '.join('case 0x%X: goto L_%06X;' % (c, c) for c in cases), a)
                self.labels.update(cases)
                return [sw]
            return ['callAddress(%s); return;' % a]
        if mn in ('rts', 'rte', 'rtr'):
            return ['return;']
        if ins.cc is not None and mn.startswith('b'):
            cond = 'CC_' + CC[ins.cc].upper()
            return ['if (%s) %s' % (cond, self.target_code(fe, ins.target, body))]
        if mn.startswith('db'):
            r = 'D%d' % ops[0].reg
            cond = 'CC_' + CC[ins.cc].upper()
            t = self.target_code(fe, ins.target, body)
            if ins.cc == 1:  # dbf / dbra
                return ['setW(%s, %s - 1); if ((%s & 0xFFFF) != 0xFFFF) %s' % (r, r, r, t)]
            return ['if (!%s) { setW(%s, %s - 1); if ((%s & 0xFFFF) != 0xFFFF) %s }' % (cond, r, r, r, t)]
        if mn == 'trap':
            return ['/* trap #%d ignored */' % ops[0].val]
        raise NotImplementedError('%06X %s' % (ins.addr, ins))

    def rmw_simple(self, o, sz, val):
        pre, post = [], []
        return pre + [self.write(o, sz, val, pre, post)] + post

    def emit_movem(self, ins):
        a, b = ins.ops
        sz = ins.sz
        n = SZN[sz]
        if a.kind == 'reglist':  # regs -> memory
            mask = a.val
            dst = b
            if dst.kind == 'pre':
                # mask is reversed: bit 0 = a7 ... bit 15 = d0
                regs = [(15 - i) for i in range(16) if mask & (1 << i)]
                # stored from highest register down
                lines = []
                for r in regs:
                    rn = xreg(r)
                    lines.append('A%d -= %d; wr%d(A%d, %s);' % (dst.reg, n, n * 8, dst.reg, rn))
                return ['{ ' + ' '.join(lines) + ' }']
            regs = [i for i in range(16) if mask & (1 << i)]
            pre, post = [], []
            base = self.addr_expr(dst, sz, pre, post)
            lines = ['uint32_t ea = %s;' % base]
            for r in regs:
                lines.append('wr%d(ea, %s); ea += %d;' % (n * 8, xreg(r), n))
            return ['{ ' + ' '.join(lines) + ' }']
        # memory -> regs
        mask = b.val
        src = a
        regs = [i for i in range(16) if mask & (1 << i)]
        lines = []
        if src.kind == 'post':
            lines.append('uint32_t ea = A%d;' % src.reg)
        else:
            pre, post = [], []
            lines.append('uint32_t ea = %s;' % self.addr_expr(src, sz, pre, post))
        for r in regs:
            v = 'rd%d(ea)' % (n * 8)
            if n == 2:
                v = 'sxw(%s)' % v
            lines.append('%s = %s; ea += %d;' % (xreg(r), v, n))
        if src.kind == 'post':
            lines.append('A%d = ea;' % src.reg)
        return ['{ ' + ' '.join(lines) + ' }']

    def emit_function(self, e, body, tails):
        self.labels = set()
        for a in body:
            ins = self.p.insns[a]
            if ins.target is not None and ins.target in body and ins.mn not in ('bsr', 'jsr'):
                self.labels.add(ins.target)
            if ins.mn == 'jmp' and ins.target is None:
                self.labels.update(c for c in self.p.code_consts if c in body)
        out = []
        order = sorted(body)
        lines_by_addr = {}
        for a in order:
            ins = self.p.insns[a]
            live = self.p.live_out.get((e, a), True)
            try:
                lines_by_addr[a] = self.emit_insn(e, body, ins, live)
            except NotImplementedError as ex:
                lines_by_addr[a] = ['#error untranslated %s' % ex]
        name = self.fname(e)
        comment = self.p.cfg.get('comments', {}).get('%X' % e) or self.p.cfg.get('comments', {}).get('%06X' % e)
        if comment:
            out.append('// ' + comment)
        out.append('void %s() {' % name)
        if order[0] != e:
            # the routine's body starts below its entry point (loops branching back)
            self.labels.add(e)
            out.append('    goto L_%06X;' % e)
        prev_end = None
        for idx, a in enumerate(order):
            ins = self.p.insns[a]
            if prev_end is not None and a != prev_end:
                # gap: previous instruction's fallthrough is not the next body insn
                pins = self.p.insns[order[idx - 1]]
                if pins.mn not in ('rts', 'rte', 'rtr', 'bra', 'jmp'):
                    out.append('    %s' % self.target_code(e, prev_end, body))
            if a in self.labels:
                out.append('L_%06X:' % a)
            txt = repr(ins)[7:]
            ls = lines_by_addr[a]
            if self.trace:
                ls = ['TRACE_PC(0x%X);' % a] + ls if ls else ['TRACE_PC(0x%X);' % a]
                ls = [' '.join(ls)]
            if not ls:
                out.append('    ; // %06X  %s' % (a, txt))
            else:
                out.append('    %s  // %06X  %s' % (ls[0], a, txt))
                for extra in ls[1:]:
                    out.append('    ' + extra)
            prev_end = a + ins.size
        last = self.p.insns[order[-1]]
        if last.mn not in ('rts', 'rte', 'rtr', 'bra', 'jmp'):
            out.append('    %s' % self.target_code(e, prev_end, body))
        out.append('}')
        return out


def main():
    args = sys.argv[1:]
    trace_out = None
    if '--trace-out' in args:
        i = args.index('--trace-out')
        trace_out = args[i + 1]
        del args[i:i + 2]
    cfg = json.load(open(args[0]))
    sys.argv[1] = args[0]
    base_dir = os.path.dirname(os.path.abspath(sys.argv[1]))
    if not os.path.isabs(cfg['adf']):
        cfg['adf'] = os.path.join(base_dir, cfg['adf'])
    p = Program(cfg)
    p.code_ranges = [(int(a, 16), int(b, 16)) for a, b in cfg['code_ranges']]
    for t in cfg.get('tables', []):
        a = int(t['addr'], 16)
        for i in range(t['count']):
            v = p.image_of(a).l(a + i * t.get('stride', 4) + t.get('offset', 0))
            if v:
                p.entries.add(v)
                p.code_consts.add(v)
    p.discover()
    # every lea'd code label becomes a code constant and a function entry
    p.build_functions()
    p.liveness()
    em = Emitter(p, trace=trace_out is not None)
    out_dir = cfg['out_dir'] if os.path.isabs(cfg['out_dir']) else os.path.join(base_dir, cfg['out_dir'])
    if trace_out:
        out_dir = os.path.abspath(trace_out)
    os.makedirs(out_dir, exist_ok=True)
    funcs = sorted(p.funcs)
    hdr = ['// Generated by tools/m68k2cpp.py from the original 68000 code. Do not edit by hand;',
           '// hand-written replacements live in natives.cpp / the *.cpp files next to this one.',
           '#pragma once', '', 'namespace game {']
    for e in funcs:
        hdr.append('void %s();' % em.fname(e))
    for a, nm in sorted(p.natives.items()):
        hdr.append('void %s();  // native replacement of $%06X' % (nm, a))
    hdr.append('}  // namespace game')
    open(os.path.join(out_dir, cfg.get('header', 'generated.hpp')), 'w').write('\n'.join(hdr) + '\n')
    # split into files per image
    by_image = {}
    for e in funcs:
        by_image.setdefault(p.image_of(e).name, []).append(e)
    for imname, es in by_image.items():
        lines = ['// Generated by tools/m68k2cpp.py from %s. Do not edit by hand.' % imname,
                 '#include "game/cpu.hpp"', '#include "game/generated/%s"' % cfg.get('header', 'generated.hpp'), '',
                 'namespace game {', '']
        for e in es:
            body, tails = p.funcs[e]
            lines += em.emit_function(e, body, tails)
            lines.append('')
        lines.append('}  // namespace game')
        open(os.path.join(out_dir, 'gen_%s.cpp' % imname.lower()), 'w').write('\n'.join(lines) + '\n')
    # dispatcher
    d = ['// Generated by tools/m68k2cpp.py. Address -> routine dispatch for computed jumps/calls.',
         '#include <cstdio>', '#include <cstdlib>', '#include "game/cpu.hpp"',
         '#include "game/generated/%s"' % cfg.get('header', 'generated.hpp'), '', 'namespace game {', '',
         'void callAddress(uint32_t addr) {', '    switch (addr) {']
    for e in funcs:
        d.append('    case 0x%X: %s(); return;' % (e, em.fname(e)))
    for a, nm in sorted(p.natives.items()):
        d.append('    case 0x%X: %s(); return;' % (a, nm))
    d += ['    default:', '        std::fprintf(stderr, "callAddress: no routine at $%06X\\n", unsigned(addr));',
          '        std::abort();', '    }', '}', '', '}  // namespace game']
    open(os.path.join(out_dir, 'gen_dispatch.cpp'), 'w').write('\n'.join(d) + '\n')
    # list of translated instructions (for code coverage reports of the tests)
    cov_path = cfg.get('insn_list')
    if cov_path and not trace_out:
        cov_path = cov_path if os.path.isabs(cov_path) else os.path.join(base_dir, cov_path)
        os.makedirs(os.path.dirname(cov_path), exist_ok=True)
        owner = {}
        for e in funcs:
            for a in p.funcs[e][0]:
                owner.setdefault(a, e)
        with open(cov_path, 'w') as f:
            for a in sorted(p.code):
                f.write('%06X %s\n' % (a, em.fname(owner.get(a, a))))
    print('functions: %d, instructions: %d' % (len(funcs), len(p.code)))


if __name__ == '__main__':
    main()
