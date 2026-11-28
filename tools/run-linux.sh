#!/bin/sh
# Starts PHOSPHOR on Linux. Arguments are passed on (e.g. --size 1280x720, --exec "DISPLAY FULL").
#
# Raspberry Pi 4 / 5 (and the uConsole's CM4): Mesa's V3D driver offers OpenGL 3.1, PHOSPHOR asks for
# 3.3 core. The shaders only use what V3D has, so the version override below lets it start until the
# OpenGL ES renderer (M9) exists. PHOS_GLFW_PLATFORM=x11 forces X11 if Wayland gives trouble.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${PHOS_BUILD:-$ROOT/build/linux-release}

if [ -d /sys/module/v3d ] || grep -qi "raspberry pi" /proc/device-tree/model 2>/dev/null; then
    export MESA_GL_VERSION_OVERRIDE="${MESA_GL_VERSION_OVERRIDE:-3.3}"
    export MESA_GLSL_VERSION_OVERRIDE="${MESA_GLSL_VERSION_OVERRIDE:-330}"
fi
exec "$BUILD/phosphor" "$@"
