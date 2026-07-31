# Dimension5 Plugin Wrapper

This directory is the native plugin layer. The portable realtime DSP remains in
`src/`; plugin formats only own host integration, parameter presentation, state,
presets, and UI.

## First target

- Framework: JUCE.
- First format: VST3.
- Later formats: AU/LV2/CLAP as distribution needs justify them.
- DSP source of truth: `dimension5_core`.

## Build shape

The JUCE wrapper is off by default so the embedded, CTest, and Web/WASM flows do
not require JUCE.

```sh
cmake -S . -B build-plugin -G Ninja ^
  -DDIMENSION5_BUILD_JUCE_PLUGIN=ON ^
  -DDIMENSION5_JUCE_DIR=C:/path/to/JUCE
cmake --build build-plugin --parallel
```

The preferred build path is GitHub Actions. The `CI` workflow has a
`workflow_dispatch` trigger and uploads the Windows VST3 bundle as the
`dimension5-vst3-windows` artifact.

The initial product UI promotes factory presets plus the musical controls
`Mode`, `Input`, `Output`, `Width`, `Mix`, and `Bypass`, with A/B compare and
lightweight L/R output meters for gain staging. `Bypass` crossfades to the dry
signal in the plugin wrapper while the DSP keeps running. The `Advanced` panel
exposes the main raw DSP tuning parameters and switches the plugin to
`Mode = Custom` when edited.
