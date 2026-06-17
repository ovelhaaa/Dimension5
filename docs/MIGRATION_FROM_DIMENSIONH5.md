# Migration from dimensionH5 to Dimension5

Dimension5 is the canonical repository for active development. The legacy `dimensionH5` repository should remain untouched until the deprecation phase is explicitly started.

## Comparative audit

| Area | dimensionH5 finding | Dimension5 status | Classification |
| --- | --- | --- | --- |
| GitHub Pages/public demo | Contains a web platform and prebuilt `platform/web/dist` assets. | Dimension5 has Vite + WebAudio/WASM; Pages deployment is not yet automated. | migrate later |
| CI with CMake/CTest/offline renderer | Has CMake/tests/offline runner patterns. | Dimension5 now has root CMake, CTest, native gcc/clang CI, Web/WASM CI, frontend build, and WAV artifact generation. | migrate now |
| Audio regression tests | Has test-signal and WAV helper concepts. | Dimension5 now validates initialization, silence, stereo/mono, mode switching, parameter clamping, determinism, and signal metrics without fragile binary snapshots. | already equivalent |
| Architecture documentation | Has several design/review docs. | Dimension5 README and this migration guide document architecture and deprecation plan; deeper tuning docs can be migrated later. | migrate later |
| Offline WAV rendering | Has `tests/offline_runner.c` and WAV helpers. | Dimension5 now has `dimension5_offline_render` with a minimal C WAV writer and generated mode files. | migrate now |
| README/roadmap | Legacy repo contains broader planning notes. | Dimension5 README now states canonical status, quickstarts, CI/test/render flow, roadmap, limitations, and deprecation plan. | migrate now |
| Examples of use | Legacy repo has STM32H5 app/platform examples and web examples. | Dimension5 keeps a smaller STM32H562 example and web demo; full HAL/CubeMX app should not be copied wholesale. | migrate later |
| Large audio samples | Legacy repo stores several WAV samples. | Dimension5 uses deterministic generated signals to keep the canonical repo lightweight. | discard |
| Separate modular DSP files | Legacy repo splits filters/interpolation/modes. | Dimension5 intentionally keeps a single C core for the current embedded/WASM target; split only if it improves maintainability without duplicating DSP. | migrate later |

## What was migrated/prepared

- Root CMake build with `dimension5_core`, `dimension5_tests`, and `dimension5_offline_render` targets.
- Native CI for GCC/Clang with CTest and optional sanitizer flags.
- Web/WASM CI using Emscripten plus Vite production build.
- Offline WAV renderer that creates `out/dimension5_mode1.wav` through `out/dimension5_mode4.wav`.
- Documentation that identifies Dimension5 as the source of truth.

## What will not be migrated

- Large bundled sample WAV files from `dimensionH5`; deterministic generated stimuli are preferred for CI.
- Generated CubeMX/HAL projects unless a minimal, reviewed firmware skeleton is requested.
- A second JavaScript DSP implementation; browser code must only host UI, buffers, parameters, WebAudio, and WASM loading.

## API differences

- Dimension5 public C API is `Dimension_Init`, `Dimension_Reset`, `Dimension_SetMode`, `Dimension_SetParams`, `Dimension_GetParams`, and planar `Dimension_ProcessBlock`.
- Dimension5 uses caller-owned `DimensionDSP` storage and avoids dynamic allocation in realtime processing.
- Web integration calls the C core through `src/dimension_wasm_bridge.c`; no DSP is duplicated in JavaScript.

## Build differences

- Native host builds use root CMake: `cmake -S . -B build && cmake --build build`.
- Tests use CTest: `ctest --test-dir build --output-on-failure`.
- Web/WASM builds use `web/scripts/build-wasm.sh` followed by the Vite build in `web/`.

## Suggested migration path for dimensionH5 users

1. Replace direct includes of legacy core headers with `src/dimension_dsp.h` from Dimension5.
2. Allocate one `DimensionDSP` per audio instance in static or owned host storage.
3. Convert audio callbacks to planar float buffers before calling `Dimension_ProcessBlock`.
4. Keep platform/HAL code outside the DSP core.
5. For browser deployments, use the Dimension5 Vite/WebAudio/WASM demo as the reference integration.

## Deprecation timeline

- **Phase 1:** Dimension5 receives missing build, CI, docs, renderer, and migration guidance.
- **Phase 2:** `dimensionH5` receives a README notice pointing users to Dimension5.
- **Phase 3:** `dimensionH5` is archived or redirected after users have had time to migrate.

Suggested README notice for `dimensionH5`:

> Este repositório está em modo manutenção/depreciação. O desenvolvimento ativo foi consolidado em https://github.com/ovelhaaa/Dimension5. Use Dimension5 para builds nativos, STM32H562 e Web/WASM.
