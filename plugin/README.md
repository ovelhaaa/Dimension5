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

The initial plugin exposes the existing DSP parameters directly. The product UI
should later promote a small set of musical controls and keep raw DSP controls in
an advanced panel.
