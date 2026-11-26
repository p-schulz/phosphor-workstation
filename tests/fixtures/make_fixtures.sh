#!/bin/sh
# Regenerates the audio fixtures in this folder. All content is synthesised by tools/fixturegen
# (no third-party audio). Needs: a built fixturegen, macOS afconvert, flac, lame, ffmpeg.
#   sh tests/fixtures/make_fixtures.sh build/mac-debug/fixturegen
set -e
gen=${1:?path to fixturegen}
dir=$(cd "$(dirname "$0")" && pwd)
"$gen" "$dir"
src="$dir/tone_44k_stereo.wav"
afconvert -f AIFF -d BEI16 "$src" "$dir/tone_A3.aiff"
flac --silent --force -o "$dir/tone_A3.flac" "$src"
lame --quiet -b 128 "$src" "$dir/tone_A3.mp3"
ffmpeg -loglevel error -y -i "$src" -c:a vorbis -strict experimental -ac 2 "$dir/tone_A3.ogg"
rm "$src"
ls -l "$dir"
