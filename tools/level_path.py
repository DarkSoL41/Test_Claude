#!/usr/bin/env python3
"""Shortest walkable path between two cells of a level (for difftest --autopilot).

usage: level_path.py <image.adf> <level> <x1> <y1> <x2> <y2>
Walkable: empty, base, infotron, red disk, bug; everything else blocks.
"""
import sys
from collections import deque

sys.path.insert(0, __import__('os').path.dirname(__file__))
from adflib import Adf  # noqa: E402


def main():
    adf, n = Adf(sys.argv[1]), int(sys.argv[2])
    x1, y1, x2, y2 = map(int, sys.argv[3:7])
    lv = adf.read('PHIL_%02X' % (n + 0x0F))
    walk = {0x00, 0x02, 0x03, 0x04, 0x14, 0x19, 0x07}
    prev = {(x1, y1): None}
    q = deque([(x1, y1)])
    while q:
        c = q.popleft()
        if c == (x2, y2):
            break
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = c[0] + dx, c[1] + dy
            if 0 <= nx < 60 and 0 <= ny < 24 and (nx, ny) not in prev and lv[ny * 60 + nx] in walk:
                prev[(nx, ny)] = c
                q.append((nx, ny))
    path = []
    c = (x2, y2)
    while c is not None:
        path.append(c)
        c = prev.get(c)
    for x, y in reversed(path[:-1]):
        print(x, y)


if __name__ == '__main__':
    main()
