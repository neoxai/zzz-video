#!/usr/bin/env bash
set -e

# ============================================================================
#  MODE — what this flash puts on the device
#
#    STATIC   one video, looping forever. No touchscreen or GPIO input.
#             Plays STATIC_VIDEO.
#
#    DYNAMIC  every .mjpeg in DYNAMIC_DIR, swipe left/right to switch
#             (alphabetical order, wrapping). Touchscreen devices only.
# ============================================================================
MODE="DYNAMIC"

STATIC_VIDEO="/home/john/Documents/zzzFootage/368-448/s-rank.mjpeg"
DYNAMIC_DIR="/home/john/Documents/zzzFootage/368-448"
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

case "$MODE" in
    STATIC)  PIO_ENV="static" ;;
    DYNAMIC) PIO_ENV="dynamic" ;;
    *) echo "MODE must be STATIC or DYNAMIC (got '$MODE')" >&2; exit 1 ;;
esac

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

# Videos are MJPEG as exported by the web app's Finalize. data/ is rebuilt
# from scratch each time, then flashed to the internal LittleFS partition.
mkdir -p data
rm -f data/*.mjpeg
if [ "$MODE" = "STATIC" ]; then
    cp "$STATIC_VIDEO" data/
else
    shopt -s nullglob
    videos=("$DYNAMIC_DIR"/*.mjpeg)
    if [ "${#videos[@]}" -eq 0 ]; then
        echo "No .mjpeg files in $DYNAMIC_DIR" >&2
        exit 1
    fi
    cp "${videos[@]}" data/
fi

# LittleFS partition is 0xBE0000 bytes (partitions.csv); keep ~5% for
# filesystem overhead.
FS_LIMIT=$(( 0xBE0000 * 95 / 100 ))
total=$(cat data/*.mjpeg | wc -c)
echo "Mode $MODE: $(ls data/*.mjpeg | wc -l) video(s), $(( total / 1024 )) KB of $(( FS_LIMIT / 1024 )) KB"
ls -1 data/*.mjpeg | sed 's/^/  /'
if [ "$total" -gt "$FS_LIMIT" ]; then
    echo "Videos don't fit in the LittleFS partition" >&2
    exit 1
fi

pio run -e "$PIO_ENV" --target uploadfs 2>&1 | tee upload.log
pio run -e "$PIO_ENV" --target upload 2>&1 | tee -a upload.log

# PlatformIO >= 6.2 refuses to monitor when stdout is a pipe, so run it under
# `script` to give it a real TTY while still appending to upload.log.
script -q -a -c "pio device monitor -e $PIO_ENV" upload.log
