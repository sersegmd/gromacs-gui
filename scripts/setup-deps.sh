#!/usr/bin/env bash
# Install build dependencies for Molecular Studio on Debian / Ubuntu / Kali.
set -euo pipefail

if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
else
    SUDO="sudo"
fi

$SUDO apt update
$SUDO apt install -y \
    build-essential \
    cmake \
    git \
    qt6-base-dev \
    libgl1-mesa-dev

# Optional (used by the simulation side, not needed to build the GUI):
# $SUDO apt install -y gromacs

echo "Done. Build with: cmake -B build && cmake --build build -j\$(nproc)"
