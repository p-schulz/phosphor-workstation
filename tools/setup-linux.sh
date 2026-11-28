#!/bin/sh
# Builds PHOSPHOR on Debian / Raspberry Pi OS (64-bit, with desktop): installs the packages, sets the
# real-time limits for the audio group, configures, builds and runs the tests.
#
#   tools/setup-linux.sh [release|debug]     (default: release)
#
# Needs internet access (CMake fetches the pinned dependencies from GitHub) and sudo for the packages.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CONFIG=${1:-release}
cd "$ROOT"

echo "== Packages"
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git pkg-config \
    libasound2-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols libgl-dev libegl-dev \
    mesa-utils

echo "== Real-time limits (audio and MIDI threads; active after the next login)"
LIMITS=/etc/security/limits.d/95-phosphor.conf
if [ ! -f "$LIMITS" ]; then
    printf '@audio - rtprio 95\n@audio - memlock unlimited\n' | sudo tee "$LIMITS" >/dev/null
fi
if ! id -nG "$USER" | grep -qw audio; then
    sudo usermod -aG audio "$USER"
    echo "   added $USER to the audio group: log out and in once before running PHOSPHOR"
fi

echo "== Build ($CONFIG)"
MEM_MB=$(awk '/MemTotal/ {print int($2 / 1024)}' /proc/meminfo)
JOBS=$(nproc)
if [ "$MEM_MB" -lt 3000 ]; then
    JOBS=2 # the heavier files need ~700 MB each while compiling
fi
cmake --preset "linux-$CONFIG" -DPHOS_NATIVE=ON # tuned for this board's CPU
cmake --build --preset "linux-$CONFIG" -j "$JOBS"

echo "== Tests"
if ctest --preset "linux-$CONFIG" -j "$JOBS"; then
    echo "== All tests passed."
else
    echo "== SOME TESTS FAILED: please send the output above."
fi
echo "Start PHOSPHOR with: tools/run-linux.sh"
