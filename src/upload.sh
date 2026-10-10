#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VENV_DIR="../.venv"

if [ -d "$VENV_DIR" ]; then
    echo "Activating existing virtual environment..."
else
    echo "Creating virtual environment..."
    python3 -m venv "$VENV_DIR"
fi

source "$VENV_DIR/bin/activate"

if [ ! -f "$VENV_DIR/bin/pio" ]; then
    echo "Installing PlatformIO..."
    pip install platformio
fi

echo "PlatformIO version: $(pio --version)"
echo "Venv ready."

pio run --target upload --target monitor 2>&1 | tee upload.log
