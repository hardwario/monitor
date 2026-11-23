#!/usr/bin/env bash
# Plan 0044 D1: runs the HARDWARIO Terminal (relay + web Terminal, ~/terminal) locally in Docker for
# repeatable interop checks of `hwio remote` / the GUI against the real relay code.
#
#   tools/remote/local-terminal.sh [TERMINAL_SOURCE] [PORT]     (defaults: ~/terminal, 5000)
#   tools/remote/local-terminal.sh --stop
#
# The source is copied to a temporary directory first: the Terminal checkout is never modified.
# The image is built from its own Dockerfile (needs the network for node:22-slim and the npm
# packages, not terminal.hardwario.com). The server listens on http://127.0.0.1:PORT; Debug builds
# of Monitor accept this loopback http relay (HWIO_TEST_OVERRIDES), release builds accept https only:
#   build/linux-debug/app/cli/hwio remote host --relay http://127.0.0.1:5000
#   build/linux-debug/app/cli/hwio remote join CODE --relay http://127.0.0.1:5000
# The web Terminal is at http://127.0.0.1:PORT/ (Chrome; Web Bluetooth needs localhost or https).
set -euo pipefail

name=hwio-local-terminal
if [[ "${1:-}" == "--stop" ]]; then
    docker rm -f "$name" >/dev/null 2>&1 || true
    exit 0
fi
source_dir=${1:-$HOME/terminal}
port=${2:-5000}
[[ -f "$source_dir/server/index.js" ]] || { echo "No Terminal source in $source_dir" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# A copy without local build output (never write into the Terminal checkout).
tar -C "$source_dir" --exclude=node_modules --exclude=dist --exclude=.git -cf - . | tar -C "$work" -xf -
# The Terminal's own Dockerfile (relay + web Terminal). When it does not build (2026-11-10: pnpm 10
# refuses esbuild's build script, "pnpm approve-builds"), fall back to the relay alone: the
# unchanged server/ with the server dependencies of package.json (no web Terminal UI then).
if ! nice -n 19 ionice -c 3 docker build -t "$name" "$work"; then
    echo "The Terminal's Dockerfile did not build; building the relay only (server/index.js)" >&2
    deps=$(cd "$work" && node_versions=$(grep -E '"(fastify|@fastify/static|socket.io|pino)"' package.json |
        sed -E 's/^ *"([^"]+)": *"\^?([^"]+)".*/\1@\2/' | tr '\n' ' '); echo "$node_versions")
    cat > "$work/Dockerfile.relay" <<RELAY
FROM node:22-slim
WORKDIR /app
RUN npm install --omit=dev $deps
COPY server/ ./server/
RUN mkdir -p dist && echo '<p>HARDWARIO Terminal relay only (local)</p>' > dist/index.html
EXPOSE 5000
CMD ["node", "./server/index.js"]
RELAY
    nice -n 19 ionice -c 3 docker build -t "$name" -f "$work/Dockerfile.relay" "$work"
fi
docker rm -f "$name" >/dev/null 2>&1 || true
docker run -d --name "$name" -p "127.0.0.1:$port:5000" -e LOG_LEVEL=debug "$name" >/dev/null
for _ in $(seq 1 30); do
    if curl -fsS -o /dev/null "http://127.0.0.1:$port/socket.io/?EIO=4&transport=polling"; then
        echo "HARDWARIO Terminal runs on http://127.0.0.1:$port (docker logs -f $name; stop: $0 --stop)"
        exit 0
    fi
    sleep 1
done
echo "The server did not start; see: docker logs $name" >&2
exit 1
