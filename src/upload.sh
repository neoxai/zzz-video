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

# Startup video, as exported by the web app's Finalize (MJPEG, 30fps).
# data/ is flashed to the internal LittleFS partition.
VIDEO_SRC="/home/john/Documents/zzzFootage/368-448/s-rank.mjpeg"
mkdir -p data
echo "Copying $VIDEO_SRC -> data/s-rank.mjpeg"
cp "$VIDEO_SRC" data/s-rank.mjpeg

pio run --target uploadfs 2>&1 | tee upload.log
pio run --target upload 2>&1 | tee -a upload.log

# PlatformIO >= 6.2 refuses to monitor when stdout is a pipe, so run it under
# `script` to give it a real TTY while still appending to upload.log.
script -q -a -c "pio device monitor" upload.log
