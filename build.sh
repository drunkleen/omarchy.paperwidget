#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Checking dependencies..."

if ! command -v gcc &>/dev/null; then
    echo "ERROR: gcc not found. Install with: sudo pacman -S gcc"
    exit 1
fi

if ! pkg-config --exists libpulse-simple 2>/dev/null; then
    echo "ERROR: libpulse-simple not found. Install with: sudo pacman -S libpulse"
    exit 1
fi

if ! pkg-config --exists fftw3f 2>/dev/null; then
    echo "ERROR: fftw3f not found. Install with: sudo pacman -S fftw"
    exit 1
fi

echo "Building spectrum analyzer..."
gcc -O2 -o "$SCRIPT_DIR/analyzer" "$SCRIPT_DIR/analyzer.c" \
    -lpulse-simple -lpulse -lfftw3f -lm

echo "Built: $SCRIPT_DIR/analyzer"
echo "Done."
