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

set -o pipefail
pio run --target upload 2>&1 | tee upload.log

# PlatformIO >= 6.2 refuses to monitor when stdout is a pipe, so run it under
# `script` to give it a real TTY while still appending to upload.log.
script -q -a -c "pio device monitor" upload.log
