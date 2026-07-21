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
`Mode`, `Input`, `Output`, `Width`, and `Mix`, with lightweight L/R output
meters for gain staging. Raw DSP parameters remain exposed to the host and are
most useful with `Mode = Custom`; a later advanced panel can surface them
directly.
