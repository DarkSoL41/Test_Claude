#!/bin/sh
# Targeted comparison scenarios (menu, level completion, game won, red disk,
# special port) — original 68000 code vs port, frame by frame.
# usage: tests/run_scenarios.sh <build-dir> <adf>
B=${1:-build}; ADF=${2:-"../Supaplex (1991).adf"}; D=tests/data
set -e
"$B/difftest" --adf "$ADF" --frames 7900 --input $D/menu_tour.txt
"$B/difftest" --adf "$ADF" --frames 7900 --input $D/menu_tour.txt --hiscores clean
"$B/difftest" --adf "$ADF" --frames 6000 --level 2 --input $D/start_fire.txt --autopilot $D/level2_path.txt
"$B/difftest" --adf "$ADF" --frames 3000 --level 111 --clear-skips --input $D/start_fire.txt --poke 800 112D2 1
"$B/difftest" --adf "$ADF" --frames 1400 --level 26 --input $D/start_fire.txt --autopilot $D/level26_reddisk.txt
"$B/difftest" --adf "$ADF" --frames 1600 --level 101 --input $D/start_fire.txt --poke 780 12A0C 0 --autopilot $D/level101_port.txt
