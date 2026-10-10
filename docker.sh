#!/usr/bin/env bash
# Build and start the web container, then open it in the browser.
set -euo pipefail

cd "$(dirname "$0")"

IMAGE="zzz-video-web"
CONTAINER="zzz-video-web"
FOOTAGE_DIR="/home/john/Documents/zzzFootage"
PORT=3000
URL="http://localhost:$PORT"

docker build -t "$IMAGE" web

# Replace any previous container
docker rm -f "$CONTAINER" >/dev/null 2>&1 || true

# Under rootless Podman, map your user to the container's node user (uid 1000)
# so new files can be written to the footage folder as you.
USERNS_ARGS=()
if docker --version 2>/dev/null | grep -qi podman; then
  USERNS_ARGS=(--userns=keep-id)
fi

docker run -d \
  --name "$CONTAINER" \
  --init \
  "${USERNS_ARGS[@]}" \
  -p "$PORT:3000" \
  -v "$FOOTAGE_DIR:/footage" \
  "$IMAGE"

# Wait for the server to respond before opening the browser
for _ in $(seq 1 30); do
  if curl -fs "$URL/api/files" >/dev/null; then
    break
  fi
  sleep 1
done

xdg-open "$URL" >/dev/null 2>&1 &

echo "Running at $URL (logs: docker logs -f $CONTAINER, stop: docker rm -f $CONTAINER)"
