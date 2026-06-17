# Dimension5

Dimension5 is the canonical repository for the Dimension chorus project. It consolidates the embedded C DSP core, STM32H562 integration notes, and the Web/WASM demo that will replace the older `ovelhaaa/dimensionH5` repository over a staged deprecation period.

## Status

- Single DSP source of truth: `src/dimension_dsp.c` / `src/dimension_dsp.h`.
- Browser API bridge: `src/dimension_wasm_bridge.c`.
- Web build: `web/scripts/build-wasm.sh` and Vite frontend.
- Primary embedded target: `platform/stm32h562`.
- CI-ready native CMake/CTest and offline WAV rendering.

## Native quickstart

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Optional host hardening:

```bash
cmake -S . -B build-asan -DDIMENSION5_ENABLE_ASAN=ON -DDIMENSION5_ENABLE_UBSAN=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

`DIMENSION5_ENABLE_FAST_MATH` is `OFF` by default and should only be enabled intentionally after listening and numeric validation.

## Offline render

Generate deterministic WAVs for auditory checks and CI artifacts:

```bash
cmake --build build --target dimension5_offline_render
./build/dimension5_offline_render out
```

Outputs:

- `out/dimension5_mode1.wav`
- `out/dimension5_mode2.wav`
- `out/dimension5_mode3.wav`
- `out/dimension5_mode4.wav`

## Web/WASM quickstart

Dependencies: Node.js 18+, npm, and Emscripten (`emcc`) on `PATH`.

```bash
cd web
npm ci
npm run build:wasm
npm run dev
```

Production build:

```bash
cd web
npm ci
npm run build:wasm
npm run build
```

The web application uses WebAudio and WASM. JavaScript owns UI, file/audio routing, buffers, parameters, and error messages; DSP stays in the C core.

## Architecture

```text
src/dimension_dsp.c/.h          portable C99 realtime DSP core
src/dimension_wasm_bridge.c     narrow browser/WASM ABI
web/scripts/build-wasm.sh       Emscripten build entry point
web/src/audio/engine.js         WebAudio orchestration and WASM loading
web/src/main.js                 UI and parameter mapping
platform/stm32h562              embedded integration notes/examples
tests/test_dimension_core.c     host regression tests
tools/dimension5_offline_render.c  deterministic WAV renderer
```

Realtime constraints:

- no malloc/free in `Dimension_ProcessBlock`;
- no printf/logging in the audio hot path;
- no HAL dependency in the core;
- C99-compatible implementation;
- public input and parameters are clamped/sanitized defensively.

## Tests

The C tests cover initialization, silence, mono/stereo processing, non-finite safety, extreme parameters, mode changes, smoothing behavior, delay interpolation, sample-rate variation, buffer limits, determinism, and signal metric regression ranges.

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## STM32H562 notes

The DSP core is platform independent. Keep STM32 HAL/LL, SAI/I2S, DMA, and codec code in `platform/stm32h562` or firmware-specific projects.

Critical bring-up topics:

- DMA circular or double-buffered audio transfer;
- cache coherency for DMA buffers;
- 32-byte alignment where cache/DMA require it;
- Cortex-M33 FPU enabled in the toolchain and startup code;
- float32 processing internally with codec conversion at the platform boundary;
- recommended block size: 32 frames; supported host block chunks: 16, 32, or 64 frames.

## Migration/deprecation plan for dimensionH5

See `docs/MIGRATION_FROM_DIMENSIONH5.md` for the comparative audit and user migration guidance.

Suggested deprecation phases:

1. Dimension5 receives missing CI, tests, docs, and renderer support.
2. `dimensionH5` gets a README maintenance/deprecation notice.
3. `dimensionH5` is archived or redirected after the transition window.

Suggested notice for the old README:

> Este repositório está em modo manutenção/depreciação. O desenvolvimento ativo foi consolidado em https://github.com/ovelhaaa/Dimension5. Use Dimension5 para builds nativos, STM32H562 e Web/WASM.

## Roadmap

- Add GitHub Pages deployment for the public demo.
- Expand architectural/tuning documentation from the useful parts of `dimensionH5`.
- Validate AudioWorklet latency and keep compatibility fallbacks where needed.
- Add hardware timing measurements for STM32H562 DMA callbacks.
- Add lightweight examples for native hosts without adding heavy dependencies.

## Known limitations

- Browser microphone and autoplay behavior depend on user gestures and permissions.
- Web latency depends on browser, OS, audio device, and AudioWorklet scheduling.
- STM32H562 firmware is represented by integration notes/examples, not a generated CubeMX project.
- Offline WAV regression uses numeric metric ranges, not fragile binary snapshots.

## License

No license file is currently present. Add an explicit license before broad redistribution.
