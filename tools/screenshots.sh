#!/bin/sh
# Screenshots for the README: every page of the running app (CRT pass on) in the GREEN and the
# COLOR theme, plus one page in each other theme, with content from the example projects
# (examples/projects) or the demo fixture. Writes docs/img/phosphor-*.png.
#
#   tools/screenshots.sh [build dir]   (default build/mac-release; build `phosphor` and `midi_probe`)
#
# Works on a scratch copy of the projects and a scratch settings folder, so nothing in the repo or in
# your settings changes. Audio plays at MASTER -60 dB (meters and loops need the engine running).
# Two midi_probe processes stand in for a keyboard (notes into Page 3 / K) and a synth (an output on
# Page 3 OUT).
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$ROOT/build/mac-release}
APP=$BUILD/phosphor
PROBE=$BUILD/midi_probe
OUT=$ROOT/docs/img
WORK=$(mktemp -d "${TMPDIR:-/tmp}/phosphor-shots.XXXXXX")
PIDS=""
trap 'for p in $PIDS; do kill "$p" 2>/dev/null || true; done; rm -rf "$WORK"' EXIT

# A layout like the real one (Page 2 shows the end of the path): the library where LIBRARY looks.
DOCS="$WORK/Volumes/Macintosh HD/Users/you/Documents/PHOSPHOR"
mkdir -p "$WORK/config" "$DOCS" "$OUT"
cp -R "$ROOT/examples/projects" "$DOCS/Projects"
cp -R "$ROOT/tests/fixtures/demo.phosphor" "$DOCS/Projects/"
cp -R "$ROOT/examples/library" "$DOCS/Instruments"
rm -rf "$DOCS"/Projects/*/project.autosave.json "$DOCS"/Projects/*/autosave
cat > "$WORK/config/settings.json" <<EOF
{"schemaVersion": 1, "disk": {"folder": "$DOCS/Instruments", "projects": "$DOCS/Projects"}}
EOF

P=$DOCS/Projects
ARGS=""
add() { ARGS="$ARGS
$1"; }
# shot <file> <page command>: the page in GREEN, then in COLOR (the page command again after THEME,
# so the INFO line shows the page name, not the theme).
shot() {
    add "WAIT 40"
    add "SHOT $OUT/phosphor-$1.png"
    add "THEME COLOR"
    add "$2"
    add "WAIT 40"
    add "SHOT $OUT/phosphor-$1-color.png"
    add "THEME GREEN"
    add "$2"
}
# themed <file> <theme> <page command>: one page in another theme.
themed() {
    add "THEME $2"
    add "$3"
    add "WAIT 40"
    add "SHOT $OUT/phosphor-$1.png"
    add "THEME GREEN"
    add "$3"
}
# LOAD twice: the second confirms discarding the changes of the previous project (MASTER, FOLLOW).
load() { add "LOAD $P/$1.phosphor"; add "LOAD $P/$1.phosphor"; add "WAIT 20"; }

# GB ADVENTURE: index, composer while playing (FOLLOW), Game Boy chip page, MIDI pages.
load "GB ADVENTURE"
add "MASTER -60"
add "MIDIOUT ON"
add "MIDIOUT PORT PHOSPHOR PROBE IN ON"
add "MIDIOUT CLOCK PHOSPHOR PROBE IN ON"
add "P1";                        shot page-1 "P1"
add "PR"
add "PLAY"
add "WAIT 1000";                 shot page-r "PR"
add "STOP"
add "INST 1"
add "PC";                        shot page-c-gb "PC"
themed theme-lcd LCD "PC"
add "P3";                        shot page-3 "P3"
add "P3 OUT";                    shot page-3-out "P3 OUT"
add "PK";                        shot page-k "PK"
# SNES FIELD: SNES chip page.
load "SNES FIELD"
add "INST 1"
add "PC";                        shot page-c-snes "PC"
add "INST 3"
add "PD";                        shot page-d-sample "PD"
# WAVEFORMS: the wavetable pages (instrument 15), the synth page (supersaw).
load "WAVEFORMS"
add "INST 15"
add "P4";                        shot page-4 "P4"
add "P6";                        shot page-6 "P6"
add "P0";                        shot page-0 "P0"
add "PD";                        shot page-d "PD"
themed theme-cyan CYAN "P4"
add "INST 7"
add "P5";                        shot page-5 "P5"
# Demo fixture: the sampler pages.
load "demo"
add "INST 1"
add "P7";                        shot page-7 "P7"
add "P8";                        shot page-8 "P8"
themed theme-amber AMBER "P8"
# VAPOR PLAZA playing: loops, mixer, drum kit.
load "VAPOR PLAZA"
add "MASTER -60"
add "INST 4"
add "P5";                        shot page-5-drum "P5"
add "PLAY"
add "WAIT 1700"
add "PL";                        shot page-l "PL"
themed theme-vapor VAPOR "PL"
add "PM";                        shot page-m "PM"
add "STOP"
# Disk (the instrument library), setup, help, test card.
add "P2";                        shot page-2 "P2"
add "PS";                        shot page-s "PS"
themed theme-grey GREY "PS"
add "PH";                        shot page-h "PH"
add "TESTCARD"
add "WAIT 40"
add "SHOT $OUT/phosphor-testcard.png"
# Side keys: the window at 16:9, Page R in COLOR.
add "PR"
add "THEME COLOR"
add "DISPLAY ASPECT 16:9"
add "WAIT 60"
add "SHOT $OUT/phosphor-side-keys.png"
add "THEME GREEN"
add "QUIT"

# The stand-ins for a keyboard and a synth (CoreMIDI virtual ports).
if [ -x "$PROBE" ]; then
    "$PROBE" --listen 200 >/dev/null 2>&1 & PIDS="$PIDS $!"
    "$PROBE" --wait 2 --notes 4000 --interval 40 --note 60 --hold 0 >/dev/null 2>&1 & PIDS="$PIDS $!"
    sleep 1
fi

# One --exec per line (commands contain spaces, so split on newlines only).
IFS='
'
set -- --config-dir "$WORK/config" --size 512x384
for c in $ARGS; do
    set -- "$@" --exec "$c"
done
"$APP" "$@" | grep -E "^shot:|^exec: .*ERROR" | grep -v "UNSAVED CHANGES" || true

# README size: 800 x 600 JPEG (about 170 KB each instead of 300-400 KB of PNG). macOS sips; elsewhere
# the PNGs stay.
if command -v sips >/dev/null 2>&1; then
    for f in "$OUT"/phosphor-*.png; do
        h=600
        w=800
        case "$f" in
        *side-keys*) h=540 w=960 ;; # 16:9
        esac
        sips -z "$h" "$w" -s format jpeg -s formatOptions 85 "$f" --out "${f%.png}.jpg" >/dev/null && rm "$f"
    done
fi
