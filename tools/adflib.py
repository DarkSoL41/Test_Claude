"""Minimal reader for Amiga OFS disk images (.adf)."""
import struct


class Adf:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        self.files = {}
        self._walk(880, '')

    def _blk(self, n):
        return self.d[n * 512:(n + 1) * 512]

    @staticmethod
    def _l(b, o):
        return struct.unpack('>I', b[o:o + 4])[0]

    def _walk(self, blk, prefix):
        b = self._blk(blk)
        for i in range(72):
            h = self._l(b, 24 + 4 * i)
            while h:
                hb = self._blk(h)
                name = hb[433:433 + hb[432]].decode('latin1')
                if struct.unpack('>i', hb[508:512])[0] == 2:
                    self._walk(h, prefix + name + '/')
                else:
                    self.files[(prefix + name).upper()] = h
                h = self._l(hb, 496)

    def read(self, name):
        hb = self._blk(self.files[name.upper()])
        size = self._l(hb, 0x144)
        out = bytearray()
        blk = self._l(hb, 16)
        while blk:
            db = self._blk(blk)
            out += db[24:24 + self._l(db, 12)]
            blk = self._l(db, 16)
        return bytes(out[:size])
