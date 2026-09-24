import struct, sys
def unpack(buf):
    # buf: starts with packedlen, unpackedlen, checksum
    plen, ulen, chk = struct.unpack('>III', buf[:12])
    src = buf[12:12+plen]
    words = [struct.unpack('>I', src[i:i+4])[0] for i in range(0, plen, 4)]
    a0 = len(words)
    out = bytearray(ulen); a2 = ulen
    st = {'d0':0,'d5':chk,'a0':a0}
    def getlong():
        st['a0'] -= 1; v = words[st['a0']]; st['d5'] ^= v; return v
    d0 = getlong()
    # emulate lsr / roxr semantics
    def getbit():
        nonlocal d0
        c = d0 & 1; d0 >>= 1
        if d0 == 0:
            v = getlong(); c = v & 1; d0 = (v >> 1) | 0x80000000
        return c
    def getbits(n):
        r = 0
        for _ in range(n): r = (r << 1) | getbit()
        return r
    while a2 > 0:
        if not getbit():
            if not getbit():
                n = getbits(3); cnt = n  # literal run of n+1
                for _ in range(cnt+1):
                    a2 -= 1; out[a2] = getbits(8)
                continue
            else:
                off = getbits(8); cnt = 1  # 2 bytes
        else:
            t = getbits(2)
            if t < 2:
                off = getbits(9 + t); cnt = t + 2
            elif t == 3:
                n = getbits(8); cnt = n + 8
                for _ in range(cnt+1):
                    a2 -= 1; out[a2] = getbits(8)
                continue
            else:
                cnt = getbits(8); off = getbits(12)
        for _ in range(cnt+1):
            a2 -= 1; out[a2] = out[a2+off]
    return bytes(out), st['d5']
if __name__ == '__main__':
    d = open(sys.argv[1],'rb').read()
    off = int(sys.argv[3],16) if len(sys.argv)>3 else 0
    o, c = unpack(d[off:])
    print('len', len(o), 'checksum residue', hex(c))
    open(sys.argv[2],'wb').write(o)
