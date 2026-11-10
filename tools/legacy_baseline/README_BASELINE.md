# Dimension5 — Legacy DSP Baseline (Commit 08903b86de41)

This directory contains the authentic, frozen legacy DSP source code prior to Spatial Engine 2.0 (Phase 1).

## Provenance
- **Commit**: `08903b86de41` ("Merge branch 'ui-2.1-polish' into main")
- **Parent of Phase 1**: `b4f6dd6` / `6e1f1bc`
- **Blob SHAs**:
  - `dimension_dsp.c`: `915f348b8e141d90327e7f389cfafc10cb8e0152`
  - `dimension_dsp.h`: `5154146f1dc725d1ad8c9b27500fc01dba3361d0`
  - `dimension_params.h`: `252792cc4f4a11eea6a99db4824ec2b3f2a19a05`
  - `dimension_config.h`: `717dabe471699a94125e709b69420758339db4be`

## Verification
You can independently verify the authenticity of these files with:
```bash
git hash-object tools/legacy_baseline/dimension_dsp.c
git hash-object tools/legacy_baseline/dimension_dsp.h
git hash-object tools/legacy_baseline/dimension_params.h
git hash-object tools/legacy_baseline/dimension_config.h
```
Each hash must match the blob hash in `git ls-tree 08903b86de41 src/`.

## Purpose
Used to build `dimension5_legacy_render` which generates true ground-truth baseline audio fixtures and benchmarks to test the new Spatial Engine 2.0 implementation for zero regression when `stereoWidth = 1.0` and `bassFocusHz = 0.0`.
