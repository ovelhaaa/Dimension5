#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/web/public/wasm"
mkdir -p "${OUT_DIR}"

if ! command -v emcc >/dev/null 2>&1; then
  echo "error: emcc not found. Install/activate Emscripten before running this script." >&2
  exit 127
fi

emcc \
  "${ROOT_DIR}/src/dimension_dsp.c" \
  "${ROOT_DIR}/src/dimension_wasm_bridge.c" \
  -I"${ROOT_DIR}/src" \
  -O3 \
  -Wall -Wextra -Wshadow -Wdouble-promotion -Werror \
  -s STANDALONE_WASM=1 \
  -s EXPORTED_FUNCTIONS='["_malloc","_free","_DimensionWasm_Init","_DimensionWasm_Reset","_DimensionWasm_SetMode","_DimensionWasm_SetParam","_DimensionWasm_Process"]' \
  --no-entry \
  -o "${OUT_DIR}/dimension_dsp.wasm"

test -s "${OUT_DIR}/dimension_dsp.wasm"
echo "WASM build complete at ${OUT_DIR}"
