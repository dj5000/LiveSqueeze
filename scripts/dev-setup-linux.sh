#!/usr/bin/env bash
# Install everything needed to build and test LiveSqueeze on Debian/Ubuntu (tested on 24.04).
# Safe to run repeatedly.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    SUDO=sudo
else
    SUDO=
fi

$SUDO apt-get update
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git ffmpeg python3 \
    clang clang-format \
    libpipewire-0.3-dev libspa-0.2-dev pipewire pipewire-bin wireplumber dbus-x11 \
    qt6-base-dev libgl1-mesa-dev xvfb fonts-dejavu-core

echo "Done. Try: cmake --preset release && cmake --build --preset release && ctest --preset release"
