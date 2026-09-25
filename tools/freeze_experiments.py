#!/usr/bin/env python3
"""Experiments with the "freeze zonks" byte (level byte 1469, special ports).

Every experiment is a small level drawn in ASCII. It is written as level 1 of
a copy of the data folder, once for each mode 0 / 1 / 2, and played in the
port (supaplex --test-level 1): the port runs the game's own code, so what
happens is what happens on the Amiga. Keys can be pressed; the map is read
from the game's memory (v_map, $11928) at a few moments and printed side by
side for the three modes.

usage: freeze_experiments.py [--game port/build/supaplex] [--data data]
                             [--jobs 12] [--only NAME[,NAME...]] [--modes 0,1,2]
                             [--shots DIR]  (screenshot NAME_modeM.ppm at the last snapshot)

Legend:  ' ' empty  . base  Z zonk  I infotron  M Murphy  R RAM chip
         # hardware  E exit  O orange disk  Y yellow disk  T terminal
         D red disk  S snik snak  L electron  B bug  > v < ^ ports
         + port in all directions   } special port to the right (its setting is
         given in the experiment: gravity, freeze zonks, freeze enemies)
In the output: lower-case / other marks are cells in motion or explosion:
         * explosion ($8888 / $xx1F..)  ~ a cell being entered or left
"""
import argparse
import concurrent.futures as cf
import os
import shutil
import subprocess
import sys
import tempfile

LEGEND = {' ': 0, '.': 2, 'Z': 1, 'I': 4, 'M': 3, 'R': 5, '#': 6, 'E': 7, 'O': 8, 'Y': 18,
          'T': 19, 'D': 20, 'S': 17, 'L': 24, 'B': 25, '>': 9, 'v': 10, '<': 11, '^': 12, '+': 23, '}': 13}
CHAR = {v: k for k, v in LEGEND.items()}
V_MAP = 0x11928
LEVEL_FRAME = 460          # the level runs from this frame on
X0, Y0 = 2, 2              # where the drawing goes on the map

# name, drawing, [(frame after LEVEL_FRAME, key, 1 down / 0 up)], snapshots, note
# [, special ports: [(freeze zonks value of the ports in drawing order)]]
EXPERIMENTS = [
    # --- zonks falling onto things (the fall starts when the level starts)
    ('zonk_on_orange', ['#####', '# Z #', '#   #', '# O #', '#####'], [], [8, 30, 120], 'zonk falls onto an orange disk'),
    ('zonk_on_yellow', ['#####', '# Z #', '#   #', '# Y #', '#####'], [], [40], 'zonk falls onto a yellow disk'),
    ('zonk_on_red', ['#####', '# Z #', '#   #', '# D #', '#####'], [], [40], 'zonk falls onto a red disk'),
    ('zonk_on_bug', ['#####', '# Z #', '#   #', '# B #', '#####'], [], [40, 300], 'zonk falls onto a bug'),
    ('zonk_on_terminal', ['#####', '# Z #', '#   #', '# T #', '#####'], [], [40], 'zonk falls onto a terminal'),
    ('zonk_on_ram', ['#####', '# Z #', '#   #', '# R #', '#####'], [], [60], 'zonk falls onto a RAM chip'),
    ('zonk_on_snik', ['#####', '##Z##', '## ##', '##S##', '#####'], [], [6, 12, 20, 60, 200], 'zonk falls one cell onto a snik snak'),
    ('zonk_on_electron', ['#####', '##Z##', '## ##', '##L##', '#####'], [], [6, 12, 20, 60, 200], 'zonk falls one cell onto an electron'),
    ('zonk_on_murphy', ['#####', '##Z##', '## ##', '##M##', '#####'], [], [20, 60], 'zonk falls one cell onto Murphy'),
    ('zonk_meets_snik', ['#####', '##Z##', '## ##', '## ##', '## ##', '## ##', '##S##', '#####'], [],
     [4, 8, 12, 16, 20, 24, 28, 32, 40, 60, 120], 'zonk falls down a shaft while a snik snak climbs it'),
    # --- infotrons falling onto things
    ('infotron_on_snik', ['#####', '##I##', '## ##', '##S##', '#####'], [], [12, 20, 60], 'infotron falls one cell onto a snik snak'),
    ('infotron_on_electron', ['#####', '##I##', '## ##', '##L##', '#####'], [], [12, 20, 60], 'infotron falls one cell onto an electron'),
    ('infotron_on_orange', ['#####', '# I #', '#   #', '# O #', '#####'], [], [30, 120], 'infotron falls onto an orange disk'),
    ('infotron_meets_snik', ['#####', '##I##', '## ##', '## ##', '## ##', '## ##', '##S##', '#####'], [],
     [4, 8, 12, 16, 20, 24, 28, 32, 40, 60, 120], 'infotron falls down a shaft while a snik snak climbs it'),
    # --- explosions and zonks
    ('explosion_beside_zonks', ['#######', '#  O  #', '#     #', '# ZOZ #', '#######'], [], [20, 40, 80, 200],
     'orange disk falls onto an orange disk between two resting zonks'),
    ('orange_on_zonk', ['#####', '# O #', '#   #', '# Z #', '#####'], [], [30, 120], 'orange disk falls onto a zonk'),
    ('terminal_yellow_under_zonk', ['#######', '#MT Z #', '#.# Y #', '#######'],
     [(25, 'Right', 1), (25, 'space', 1), (60, 'Right', 0), (60, 'space', 0)], [30, 70, 120, 250],
     'Murphy presses fire + Right next to a terminal (fire may not reach the game here: unverified)'),
    # --- Murphy and zonks
    ('terminal_touch', ['#######', '#MT Z #', '#.# Y #', '#######'], [(25, 'Right', 1), (40, 'Right', 0)], [30, 50, 80, 200],
     'Murphy touches a terminal: the yellow disk under a zonk explodes'),
    ('eat_base_then_walk_under', ['#####', '##Z##', '##.##', '##M##', '##.##', '#####'],
     [(25, 'Up', 1), (35, 'Up', 0), (60, 'Down', 1), (70, 'Down', 0), (100, 'Up', 1), (110, 'Up', 0)], [45, 80, 90, 130, 200],
     'Murphy eats the base under a zonk, steps down, then walks back under it'),
    ('port_unfreezes', ['#######', '#Z#####', '# #####', '# #####', '#.#####', '#M}...#', '#######'],
     [(25, 'Right', 1), (60, 'Right', 0)], [20, 45, 80, 150], 'Murphy passes a special port that sets freeze 0', [0]),
    ('port_freezes_falling_zonk', ['####', '#Z##', '# ##', '# ##', '# ##', '# ##', '# ##', '# ##', '# ##', '# ##', '# ##',
                                   '# ##', '# ##', '# ##', '#.##', '#M}.', '####'],
     [(25, 'Right', 1), (40, 'Right', 0)], [30, 45, 60, 100, 200], 'a special port setting freeze 2 is passed while a zonk falls',
     [2]),
    ('freeze_midfall_view', ['#####', '#Z###', '# ###', '# ###', '# ###', '# ###', '# ###', '# ###', '#.###', '#M}.#', '#####'],
     [(20, 'Right', 1), (30, 'Right', 0)], [24, 40, 150], 'pushing into a blocked special port (freeze 2) while a zonk falls',
     [2]),
    ('push_zonk_right', ['#######', '#MZ   #', '#######'], [(25, 'Right', 1), (120, 'Right', 0)], [40, 80, 130],
     'Murphy pushes a zonk to the right'),
    ('push_zonk_over_hole', ['######', '#MZ  #', '### .#', '######'], [(25, 'Right', 1), (60, 'Right', 0)], [40, 80, 150],
     'Murphy pushes a zonk over a hole'),
    ('walk_under_zonk', ['######', '#  Z #', '#M   #', '######'], [(25, 'Right', 1), (70, 'Right', 0)], [30, 45, 60, 90, 150],
     'Murphy walks under a zonk that hangs over the corridor'),
    ('step_away_from_zonk', ['#####', '##Z##', '##.##', '##M##', '##.##', '##.##', '#####'], [(25, 'Down', 1), (40, 'Down', 0)],
     [30, 45, 60, 150], 'Murphy under a zonk (base between) steps down'),
    ('eat_base_under_zonk', ['#####', '##Z##', '##.##', '##M##', '#####'], [(25, 'Up', 1), (40, 'Up', 0)], [30, 45, 60, 150],
     'Murphy eats the base that holds a zonk'),
    ('infotron_eat_under', ['#####', '##I##', '##.##', '##M##', '#####'], [(25, 'Up', 1), (40, 'Up', 0)], [30, 45, 60, 150],
     'Murphy eats the base that holds an infotron'),
    # --- rolling
    ('zonk_rolls_off_zonk', ['#####', '# Z #', '# Z #', '#####'], [], [40], 'zonk on a zonk, room on both sides'),
    ('zonk_rolls_off_infotron', ['#####', '# Z #', '# I #', '#####'], [], [40], 'zonk on an infotron'),
    ('infotron_rolls_off_zonk', ['#####', '# I #', '# Z #', '#####'], [], [40], 'infotron on a zonk'),
    ('zonk_rolls_off_ram', ['#####', '# Z #', '# R #', '#####'], [], [40], 'zonk on a RAM chip'),
    ('zonk_rolls_onto_snik', ['######', '# Z  #', '#.Z  #', '#.#S #', '######'], [], [20, 60, 200],
     'zonk rolls off a zonk into a snik snak\'s corridor'),
]



def build_level(base_level, drawing, ports=()):
    lv = bytearray(base_level)
    for i in range(1440):
        lv[i] = 6
    has_murphy = any('M' in row for row in drawing)
    for dy, row in enumerate(drawing):
        for dx, ch in enumerate(row):
            lv[(Y0 + dy) * 60 + X0 + dx] = LEGEND[ch]
    if not has_murphy:
        lv[20 * 60 + 50] = 3
    lv[21 * 60 + 50] = 7
    m = lv.index(3) if 3 in lv[:1440] else 20 * 60 + 50
    lv[1440:1444] = bytes([0, min(max(m % 60 - 10, 0), 39), 0, min(max(m // 60 - 6, 0), 11)])  # start camera on Murphy
    lv[1444] = 0                       # gravity off
    lv[1446:1469] = b'EXPERIMENT'.ljust(23)
    lv[1470] = 0
    lv[1471:1536] = bytes(65)          # no special ports
    cells = [(Y0 + dy) * 60 + X0 + dx for dy, row in enumerate(drawing) for dx, ch in enumerate(row) if ch == '}']
    for i, cell in enumerate(cells):
        e = 1472 + 6 * i
        lv[e:e + 6] = bytes([(cell * 2) >> 8, (cell * 2) & 255, 0, ports[i], 0, 0])
    lv[1471] = len(cells)
    return lv


def run_one(args, exp, mode):
    name, drawing, keys, shots, _ = exp[:5]
    ports = exp[5] if len(exp) > 5 else ()
    w = max(len(r) for r in drawing)
    h = len(drawing)
    tmp = tempfile.mkdtemp(prefix='fx_')
    try:
        for f in ('INTRO.BIN', 'MAIN.BIN', 'GRAPHICS.BIN', 'HISCORE.BIN'):
            shutil.copy(os.path.join(args.data, f), tmp)
        levels = bytearray(open(os.path.join(args.data, 'LEVELS.DAT'), 'rb').read())
        lv = build_level(levels[0:1536], drawing, ports)
        lv[1469] = mode
        levels[0:1536] = lv
        open(os.path.join(tmp, 'LEVELS.DAT'), 'wb').write(levels)
        script = []
        for f, key, down in keys:
            script.append('%d key %s %d' % (LEVEL_FRAME + f, key, down))
        for s in shots:
            fr = LEVEL_FRAME + s
            for y in range(h):
                for x in range(w):
                    script.append('%d print %X' % (fr, V_MAP + 2 * ((Y0 + y) * 60 + X0 + x)))
            script.append('%d print 112DA' % fr)
        sp = os.path.join(tmp, 'input.txt')
        open(sp, 'w').write('\n'.join(script) + '\n')
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        cmd = [args.game, '--data', tmp, '--save', os.path.join(tmp, 'h.sav'), '--test-level', '1',
               '--test-input', sp, '--frames', str(LEVEL_FRAME + max(shots) + 2)]
        if args.shots:
            os.makedirs(args.shots, exist_ok=True)
            cmd += ['--shot', os.path.join(args.shots, '%s_mode%d.ppm' % (name, mode))]
        out = subprocess.run(cmd,
                             capture_output=True, text=True, env=env, timeout=300).stdout
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    values = {}
    for line in out.splitlines():
        # "frame 500: $11928 = 0006"
        parts = line.replace(':', '').split()
        if len(parts) == 5 and parts[0] == 'frame':
            values[(int(parts[1]), int(parts[2][1:], 16))] = int(parts[4], 16)
    grids = []
    for s in shots:
        fr = LEVEL_FRAME + s
        if (fr, 0x112DA) not in values:
            grids.append(None)  # the level was over (Murphy died, or the game ended the level)
            continue
        rows = []
        raw = set()
        for y in range(h):
            row = ''
            for x in range(w):
                v = values.get((fr, V_MAP + 2 * ((Y0 + y) * 60 + X0 + x)), -1)
                lo, hi = v & 0xFF, v >> 8
                if v == 0x8888 or lo == 0x1F:
                    row += '*'
                elif hi == 0 and lo in CHAR:
                    row += CHAR[lo]
                else:
                    row += '~'
                    raw.add(v)
            rows.append(row)
        rows.append(('killed ' if values[(fr, 0x112DA)] else '') + ' '.join('%04X' % v for v in sorted(raw)))
        grids.append(rows)
    return name, mode, grids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', default='port/build/supaplex')
    ap.add_argument('--data', default='data')
    ap.add_argument('--jobs', type=int, default=12)
    ap.add_argument('--only', default='')
    ap.add_argument('--modes', default='0,1,2')
    ap.add_argument('--shots', default='')
    args = ap.parse_args()
    modes = [int(m) for m in args.modes.split(',')]
    exps = [e for e in EXPERIMENTS if not args.only or e[0] in args.only.split(',')]
    results = {}
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        futs = [ex.submit(run_one, args, e, m) for e in exps for m in modes]
        for f in cf.as_completed(futs):
            name, mode, grids = f.result()
            results[(name, mode)] = grids
    for name, drawing, keys, shots, note in (e[:5] for e in exps):
        print('=== %s: %s' % (name, note))
        if keys:
            print('    keys: ' + ', '.join('%+d %s %s' % (f, k, 'down' if d else 'up') for f, k, d in keys))
        w = max(len(r) for r in drawing)
        for si, s in enumerate(shots):
            print('  frame +%d' % s)
            cols = []
            for m in modes:
                g = results[(name, m)][si]
                cols.append(['mode %d' % m] + (g if g else ['(level over)']))
            for i in range(max(len(c) for c in cols)):
                print('    ' + '   '.join((c[i] if i < len(c) else '').ljust(max(w, 12)) for c in cols))
        print()


if __name__ == '__main__':
    sys.exit(main())
