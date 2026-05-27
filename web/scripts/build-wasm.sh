#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/web/public/wasm"
mkdir -p "${OUT_DIR}"

emcc \
  "${ROOT_DIR}/src/dimension_dsp.c" \
  "${ROOT_DIR}/src/dimension_wasm_bridge.c" \
  -I"${ROOT_DIR}/src" \
  -O3 \
  -s AUDIO_WORKLET=1 \
  -s WASM_WORKERS=1 \
  -s EXPORT_ES6=1 \
  -s STANDALONE_WASM=1 \
  -s EXPORTED_FUNCTIONS='["_malloc","_free","_DimensionWasm_Init","_DimensionWasm_Reset","_DimensionWasm_SetMode","_DimensionWasm_SetParam","_DimensionWasm_Process"]' \
  --no-entry \
  -o "${OUT_DIR}/dimension_dsp.wasm"

echo "WASM build complete at ${OUT_DIR}"
