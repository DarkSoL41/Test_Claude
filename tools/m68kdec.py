# Minimal but complete MC68000 instruction decoder.
# Produces Insn objects with mnemonic, size ('b','w','l' or None) and operand list.
import struct

class EA:
    """Effective address.
    kind: 'dn','an','ind','post','pre','disp','idx','absw','absl','pcdisp','pcidx','imm',
          'sr','ccr','usp','reglist'
    """
    __slots__ = ('kind', 'reg', 'disp', 'xreg', 'xlong', 'val', 'addr')
    def __init__(self, kind, reg=None, disp=0, xreg=None, xlong=False, val=None, addr=None):
        self.kind = kind; self.reg = reg; self.disp = disp; self.xreg = xreg
        self.xlong = xlong; self.val = val; self.addr = addr
    def __repr__(self):
        k = self.kind
        if k == 'dn': return 'd%d' % self.reg
        if k == 'an': return 'a%d' % self.reg if self.reg != 7 else 'sp'
        if k == 'ind': return '(a%d)' % self.reg
        if k == 'post': return '(a%d)+' % self.reg
        if k == 'pre': return '-(a%d)' % self.reg
        if k == 'disp': return '%d(a%d)' % (self.disp, self.reg)
        if k == 'idx': return '%d(a%d,%s.%s)' % (self.disp, self.reg, xname(self.xreg), 'l' if self.xlong else 'w')
        if k == 'absw': return '$%x.w' % (self.addr & 0xffffffff)
        if k == 'absl': return '$%x.l' % self.addr
        if k == 'pcdisp': return '$%x(pc)' % self.addr
        if k == 'pcidx': return '$%x(pc,%s.%s)' % (self.addr, xname(self.xreg), 'l' if self.xlong else 'w')
        if k == 'imm': return '#$%x' % self.val
        if k == 'reglist': return 'regs(%04x)' % self.val
        return k

def xname(x):
    return ('d%d' % x) if x < 8 else ('a%d' % (x - 8))

class Insn:
    __slots__ = ('addr', 'size', 'mn', 'sz', 'ops', 'target', 'cc', 'raw')
    def __init__(self, addr):
        self.addr = addr; self.size = 2; self.mn = None; self.sz = None; self.ops = []
        self.target = None; self.cc = None
    def __repr__(self):
        s = self.mn + ('.' + self.sz if self.sz else '')
        ops = ', '.join(repr(o) for o in self.ops)
        if self.target is not None and not (self.mn in ('jsr', 'jmp') and self.ops):
            ops = (ops + ', ' if ops else '') + '$%x' % self.target
        return '%06X %s %s' % (self.addr, s, ops)

CC = ['t', 'f', 'hi', 'ls', 'cc', 'cs', 'ne', 'eq', 'vc', 'vs', 'pl', 'mi', 'ge', 'lt', 'gt', 'le']
SZ2 = {0: 'b', 1: 'w', 2: 'l'}

class Illegal(Exception):
    pass

class Decoder:
    def __init__(self, data, base):
        self.data = data; self.base = base
    def w(self, a):
        o = a - self.base
        if o < 0 or o + 2 > len(self.data): raise Illegal('oob')
        return struct.unpack('>H', self.data[o:o + 2])[0]
    def sw(self, a):
        v = self.w(a); return v - 0x10000 if v & 0x8000 else v
    def l(self, a):
        return (self.w(a) << 16) | self.w(a + 2)

    def ea(self, ins, mode, reg, sz):
        p = ins.addr + ins.size
        if mode == 0: return EA('dn', reg)
        if mode == 1: return EA('an', reg)
        if mode == 2: return EA('ind', reg)
        if mode == 3: return EA('post', reg)
        if mode == 4: return EA('pre', reg)
        if mode == 5:
            ins.size += 2; return EA('disp', reg, disp=self.sw(p))
        if mode == 6:
            ext = self.w(p); ins.size += 2
            if ext & 0x0100: raise Illegal('full ext')
            d8 = ext & 0xff; d8 = d8 - 0x100 if d8 & 0x80 else d8
            return EA('idx', reg, disp=d8, xreg=(ext >> 12) & 15, xlong=bool(ext & 0x800))
        if mode == 7:
            if reg == 0:
                ins.size += 2; return EA('absw', addr=self.sw(p) & 0xffffffff)
            if reg == 1:
                ins.size += 4; return EA('absl', addr=self.l(p))
            if reg == 2:
                ins.size += 2; return EA('pcdisp', addr=(p + self.sw(p)) & 0xffffffff)
            if reg == 3:
                ext = self.w(p); ins.size += 2
                if ext & 0x0100: raise Illegal('full ext')
                d8 = ext & 0xff; d8 = d8 - 0x100 if d8 & 0x80 else d8
                return EA('pcidx', addr=(p + d8) & 0xffffffff, xreg=(ext >> 12) & 15, xlong=bool(ext & 0x800))
            if reg == 4:
                if sz == 'l':
                    ins.size += 4; return EA('imm', val=self.l(p))
                v = self.w(p); ins.size += 2
                if sz == 'b':
                    if v & 0xff00 not in (0, 0xff00): pass
                    v &= 0xff
                return EA('imm', val=v)
        raise Illegal('bad ea %d %d' % (mode, reg))

    def decode(self, addr):
        ins = Insn(addr)
        op = self.w(addr)
        ins.raw = op
        hi = op >> 12
        f = getattr(self, 'g%d' % hi)
        f(ins, op)
        if ins.mn is None: raise Illegal('unknown %04x' % op)
        return ins

    # ---- group 0: bit ops / immediate
    def g0(self, ins, op):
        mode = (op >> 3) & 7; reg = op & 7
        if op & 0x0100:
            if mode == 1:
                # movep
                dn = (op >> 9) & 7; opm = (op >> 6) & 7
                ins.size += 2; disp = self.sw(ins.addr + 2)
                m = EA('disp', reg, disp=disp)
                ins.mn = 'movep'; ins.sz = 'w' if opm in (4, 6) else 'l'
                ins.ops = [m, EA('dn', dn)] if opm < 6 else [EA('dn', dn), m]
                return
            t = (op >> 6) & 3
            ins.mn = ['btst', 'bchg', 'bclr', 'bset'][t]
            ins.sz = 'l' if mode == 0 else 'b'
            ins.ops = [EA('dn', (op >> 9) & 7), self.ea(ins, mode, reg, 'b')]
            return
        t = (op >> 9) & 7
        if t == 4:
            ins.mn = ['btst', 'bchg', 'bclr', 'bset'][(op >> 6) & 3]
            ins.sz = 'l' if mode == 0 else 'b'
            v = self.w(ins.addr + 2) & 0xff; ins.size += 2
            ins.ops = [EA('imm', val=v), self.ea(ins, mode, reg, 'b')]
            return
        s = (op >> 6) & 3
        if s == 3: raise Illegal('g0')
        names = {0: 'ori', 1: 'andi', 2: 'subi', 3: 'addi', 5: 'eori', 6: 'cmpi'}
        if t not in names: raise Illegal('g0 t')
        ins.mn = names[t]; ins.sz = SZ2[s]
        if mode == 7 and reg == 4:
            # to ccr / sr
            v = self.w(ins.addr + 2); ins.size += 2
            ins.ops = [EA('imm', val=v & (0xff if s == 0 else 0xffff)), EA('ccr' if s == 0 else 'sr')]
            ins.sz = None
            return
        imm = self.ea(ins, 7, 4, ins.sz)
        ins.ops = [imm, self.ea(ins, mode, reg, ins.sz)]

    # ---- move
    def _move(self, ins, op, sz):
        smode = (op >> 3) & 7; sreg = op & 7
        dreg = (op >> 9) & 7; dmode = (op >> 6) & 7
        src = self.ea(ins, smode, sreg, sz)
        if dmode == 1:
            ins.mn = 'movea'
            ins.ops = [src, EA('an', dreg)]
        else:
            ins.mn = 'move'
            ins.ops = [src, self.ea(ins, dmode, dreg, sz)]
        ins.sz = sz
    def g1(self, ins, op): self._move(ins, op, 'b')
    def g2(self, ins, op): self._move(ins, op, 'l')
    def g3(self, ins, op): self._move(ins, op, 'w')

    # ---- misc
    def g4(self, ins, op):
        mode = (op >> 3) & 7; reg = op & 7
        if op == 0x4e71: ins.mn = 'nop'; return
        if op == 0x4e75: ins.mn = 'rts'; return
        if op == 0x4e73: ins.mn = 'rte'; return
        if op == 0x4e77: ins.mn = 'rtr'; return
        if op == 0x4e70: ins.mn = 'reset'; return
        if op == 0x4e76: ins.mn = 'trapv'; return
        if op == 0x4afc: ins.mn = 'illegal'; return
        if op == 0x4e72:
            ins.mn = 'stop'; ins.ops = [EA('imm', val=self.w(ins.addr + 2))]; ins.size += 2; return
        if (op & 0xfff0) == 0x4e40: ins.mn = 'trap'; ins.ops = [EA('imm', val=op & 15)]; return
        if (op & 0xfff8) == 0x4e50:
            ins.mn = 'link'; ins.ops = [EA('an', reg), EA('imm', val=self.sw(ins.addr + 2))]; ins.size += 2; return
        if (op & 0xfff8) == 0x4e58: ins.mn = 'unlk'; ins.ops = [EA('an', reg)]; return
        if (op & 0xfff8) == 0x4e60: ins.mn = 'move'; ins.sz = 'l'; ins.ops = [EA('an', reg), EA('usp')]; return
        if (op & 0xfff8) == 0x4e68: ins.mn = 'move'; ins.sz = 'l'; ins.ops = [EA('usp'), EA('an', reg)]; return
        if (op & 0xffc0) == 0x4e80:
            ins.mn = 'jsr'; ins.ops = [self.ea(ins, mode, reg, 'l')]; self._jt(ins); return
        if (op & 0xffc0) == 0x4ec0:
            ins.mn = 'jmp'; ins.ops = [self.ea(ins, mode, reg, 'l')]; self._jt(ins); return
        if (op & 0xf1c0) == 0x41c0:
            ins.mn = 'lea'; ins.sz = 'l'; ins.ops = [self.ea(ins, mode, reg, 'l'), EA('an', (op >> 9) & 7)]; return
        if (op & 0xf1c0) == 0x4180:
            ins.mn = 'chk'; ins.sz = 'w'; ins.ops = [self.ea(ins, mode, reg, 'w'), EA('dn', (op >> 9) & 7)]; return
        if (op & 0xffc0) == 0x40c0:
            ins.mn = 'move'; ins.sz = 'w'; ins.ops = [EA('sr'), self.ea(ins, mode, reg, 'w')]; return
        if (op & 0xffc0) == 0x44c0:
            ins.mn = 'move'; ins.sz = 'w'; ins.ops = [self.ea(ins, mode, reg, 'w'), EA('ccr')]; return
        if (op & 0xffc0) == 0x46c0:
            ins.mn = 'move'; ins.sz = 'w'; ins.ops = [self.ea(ins, mode, reg, 'w'), EA('sr')]; return
        if (op & 0xffc0) == 0x4800:
            ins.mn = 'nbcd'; ins.sz = 'b'; ins.ops = [self.ea(ins, mode, reg, 'b')]; return
        if (op & 0xfff8) == 0x4840: ins.mn = 'swap'; ins.sz = 'w'; ins.ops = [EA('dn', reg)]; return
        if (op & 0xffc0) == 0x4840:
            ins.mn = 'pea'; ins.sz = 'l'; ins.ops = [self.ea(ins, mode, reg, 'l')]; return
        if (op & 0xfff8) == 0x4880: ins.mn = 'ext'; ins.sz = 'w'; ins.ops = [EA('dn', reg)]; return
        if (op & 0xfff8) == 0x48c0: ins.mn = 'ext'; ins.sz = 'l'; ins.ops = [EA('dn', reg)]; return
        if (op & 0xfb80) == 0x4880:
            # movem
            sz = 'l' if op & 0x40 else 'w'
            mask = self.w(ins.addr + 2); ins.size += 2
            e = self.ea(ins, mode, reg, sz)
            ins.mn = 'movem'; ins.sz = sz
            if op & 0x0400: ins.ops = [e, EA('reglist', val=mask)]
            else: ins.ops = [EA('reglist', val=mask), e]
            return
        if (op & 0xffc0) == 0x4ac0:
            ins.mn = 'tas'; ins.sz = 'b'; ins.ops = [self.ea(ins, mode, reg, 'b')]; return
        s = (op >> 6) & 3
        if s != 3:
            t = (op >> 8) & 0xf
            names = {0: 'negx', 2: 'clr', 4: 'neg', 6: 'not', 0xa: 'tst'}
            if t in names:
                ins.mn = names[t]; ins.sz = SZ2[s]; ins.ops = [self.ea(ins, mode, reg, ins.sz)]; return
        raise Illegal('g4 %04x' % op)

    def _jt(self, ins):
        e = ins.ops[0]
        if e.kind in ('absl', 'absw', 'pcdisp'):
            ins.target = e.addr

    # ---- addq/subq/scc/dbcc
    def g5(self, ins, op):
        mode = (op >> 3) & 7; reg = op & 7
        s = (op >> 6) & 3
        if s == 3:
            cc = (op >> 8) & 15
            if mode == 1:
                ins.mn = 'db' + CC[cc]; ins.cc = cc; ins.sz = 'w'
                ins.ops = [EA('dn', reg)]
                ins.target = (ins.addr + 2 + self.sw(ins.addr + 2)) & 0xffffffff; ins.size += 2
                return
            ins.mn = 's' + CC[cc]; ins.cc = cc; ins.sz = 'b'; ins.ops = [self.ea(ins, mode, reg, 'b')]
            return
        q = (op >> 9) & 7; q = 8 if q == 0 else q
        ins.mn = 'subq' if op & 0x100 else 'addq'; ins.sz = SZ2[s]
        ins.ops = [EA('imm', val=q), self.ea(ins, mode, reg, ins.sz)]

    def g6(self, ins, op):
        cc = (op >> 8) & 15
        d = op & 0xff
        if d == 0:
            d = self.sw(ins.addr + 2); ins.size += 2
        elif d == 0xff:
            raise Illegal('bcc.l')
        else:
            d = d - 0x100 if d & 0x80 else d
        ins.target = (ins.addr + 2 + d) & 0xffffffff
        ins.mn = 'bra' if cc == 0 else ('bsr' if cc == 1 else 'b' + CC[cc])
        ins.cc = cc

    def g7(self, ins, op):
        if op & 0x100: raise Illegal('g7')
        v = op & 0xff; v = v | 0xffffff00 if v & 0x80 else v
        ins.mn = 'moveq'; ins.sz = 'l'; ins.ops = [EA('imm', val=v), EA('dn', (op >> 9) & 7)]

    def _arith(self, ins, op, name, allow_a=True):
        mode = (op >> 3) & 7; reg = op & 7; rn = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm in (3, 7):
            sz = 'w' if opm == 3 else 'l'
            ins.mn = name + 'a'; ins.sz = sz
            ins.ops = [self.ea(ins, mode, reg, sz), EA('an', rn)]
            return
        sz = SZ2[opm & 3]
        ins.sz = sz; ins.mn = name
        if opm & 4:
            ins.ops = [EA('dn', rn), self.ea(ins, mode, reg, sz)]
        else:
            ins.ops = [self.ea(ins, mode, reg, sz), EA('dn', rn)]

    def g8(self, ins, op):
        mode = (op >> 3) & 7; reg = op & 7; rn = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm == 3:
            ins.mn = 'divu'; ins.sz = 'w'; ins.ops = [self.ea(ins, mode, reg, 'w'), EA('dn', rn)]; return
        if opm == 7:
            ins.mn = 'divs'; ins.sz = 'w'; ins.ops = [self.ea(ins, mode, reg, 'w'), EA('dn', rn)]; return
        if (op & 0x1f0) == 0x100:
            ins.mn = 'sbcd'; ins.sz = 'b'
            ins.ops = [EA('pre', reg), EA('pre', rn)] if op & 8 else [EA('dn', reg), EA('dn', rn)]; return
        ins.mn = 'or'; sz = SZ2[opm & 3]; ins.sz = sz
        ins.ops = [EA('dn', rn), self.ea(ins, mode, reg, sz)] if opm & 4 else [self.ea(ins, mode, reg, sz), EA('dn', rn)]

    def g9(self, ins, op):
        opm = (op >> 6) & 7
        if (op & 0x130) == 0x100 and opm not in (3, 7):
            ins.mn = 'subx'; ins.sz = SZ2[opm & 3]; reg = op & 7; rn = (op >> 9) & 7
            ins.ops = [EA('pre', reg), EA('pre', rn)] if op & 8 else [EA('dn', reg), EA('dn', rn)]; return
        self._arith(ins, op, 'sub')

    def gA(self, ins, op): raise Illegal('line-a')
    g10 = gA

    def g11(self, ins, op):  # B: cmp/eor/cmpm
        mode = (op >> 3) & 7; reg = op & 7; rn = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm in (3, 7):
            sz = 'w' if opm == 3 else 'l'
            ins.mn = 'cmpa'; ins.sz = sz; ins.ops = [self.ea(ins, mode, reg, sz), EA('an', rn)]; return
        sz = SZ2[opm & 3]; ins.sz = sz
        if opm & 4:
            if mode == 1:
                ins.mn = 'cmpm'; ins.ops = [EA('post', reg), EA('post', rn)]; return
            ins.mn = 'eor'; ins.ops = [EA('dn', rn), self.ea(ins, mode, reg, sz)]; return
        ins.mn = 'cmp'; ins.ops = [self.ea(ins, mode, reg, sz), EA('dn', rn)]

    def g12(self, ins, op):  # C: and/mul/abcd/exg
        mode = (op >> 3) & 7; reg = op & 7; rn = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm == 3: ins.mn = 'mulu'; ins.sz = 'w'; ins.ops = [self.ea(ins, mode, reg, 'w'), EA('dn', rn)]; return
        if opm == 7: ins.mn = 'muls'; ins.sz = 'w'; ins.ops = [self.ea(ins, mode, reg, 'w'), EA('dn', rn)]; return
        if (op & 0x1f0) == 0x100:
            ins.mn = 'abcd'; ins.sz = 'b'
            ins.ops = [EA('pre', reg), EA('pre', rn)] if op & 8 else [EA('dn', reg), EA('dn', rn)]; return
        if (op & 0x1f8) == 0x140: ins.mn = 'exg'; ins.sz = 'l'; ins.ops = [EA('dn', rn), EA('dn', reg)]; return
        if (op & 0x1f8) == 0x148: ins.mn = 'exg'; ins.sz = 'l'; ins.ops = [EA('an', rn), EA('an', reg)]; return
        if (op & 0x1f8) == 0x188: ins.mn = 'exg'; ins.sz = 'l'; ins.ops = [EA('dn', rn), EA('an', reg)]; return
        ins.mn = 'and'; sz = SZ2[opm & 3]; ins.sz = sz
        ins.ops = [EA('dn', rn), self.ea(ins, mode, reg, sz)] if opm & 4 else [self.ea(ins, mode, reg, sz), EA('dn', rn)]

    def g13(self, ins, op):  # D: add/addx
        opm = (op >> 6) & 7
        if (op & 0x130) == 0x100 and opm not in (3, 7):
            ins.mn = 'addx'; ins.sz = SZ2[opm & 3]; reg = op & 7; rn = (op >> 9) & 7
            ins.ops = [EA('pre', reg), EA('pre', rn)] if op & 8 else [EA('dn', reg), EA('dn', rn)]; return
        self._arith(ins, op, 'add')

    def g14(self, ins, op):  # E: shifts
        s = (op >> 6) & 3
        names = ['as', 'ls', 'rox', 'ro']
        if s == 3:
            t = (op >> 9) & 3; mode = (op >> 3) & 7; reg = op & 7
            if op & 0x800: raise Illegal('bitfield')
            ins.mn = names[t] + ('l' if op & 0x100 else 'r'); ins.sz = 'w'
            ins.ops = [self.ea(ins, mode, reg, 'w')]
            return
        t = (op >> 3) & 3; reg = op & 7; c = (op >> 9) & 7
        ins.mn = names[t] + ('l' if op & 0x100 else 'r'); ins.sz = SZ2[s]
        if op & 0x20: ins.ops = [EA('dn', c), EA('dn', reg)]
        else: ins.ops = [EA('imm', val=8 if c == 0 else c), EA('dn', reg)]

    def g15(self, ins, op): raise Illegal('line-f')

# dispatch names for hex nibble
for _i, _n in ((10, 'gA'),):
    pass
Decoder.g10 = Decoder.gA
