#!/usr/bin/env python3
"""Merge difftest coverage bitmaps (*.raw) and report per routine.

usage: merge_coverage.py <translated_insns.txt> <out.txt> file.raw [file.raw ...]
"""
import sys


def main():
    insns = [l.split() for l in open(sys.argv[1]) if l.strip()]
    hit = bytearray(0x80000)
    for f in sys.argv[3:]:
        d = open(f, 'rb').read()
        for i, b in enumerate(d):
            if b:
                hit[i] = 1
    per = {}
    missed = []
    for a, fn in insns:
        a = int(a, 16)
        t, h = per.get(fn, (0, 0))
        per[fn] = (t + 1, h + (1 if hit[a] else 0))
        if not hit[a]:
            missed.append('%06X %s' % (a, fn))
    total = sum(t for t, h in per.values())
    covered = sum(h for t, h in per.values())
    with open(sys.argv[2], 'w') as o:
        o.write('covered %d of %d translated instructions (%.1f%%)\n\n' % (covered, total, 100.0 * covered / total))
        o.write('routines never executed:\n')
        for fn, (t, h) in sorted(per.items()):
            if h == 0:
                o.write('  %s (%d)\n' % (fn, t))
        o.write('\nroutines partially executed:\n')
        for fn, (t, h) in sorted(per.items(), key=lambda kv: kv[1][1] / kv[1][0]):
            if 0 < h < t:
                o.write('  %-40s %4d / %4d\n' % (fn, h, t))
        o.write('\nnot executed instructions:\n' + '\n'.join(missed) + '\n')
    print('covered %d of %d (%.1f%%)' % (covered, total, 100.0 * covered / total))


if __name__ == '__main__':
    main()
