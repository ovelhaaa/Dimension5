# Frozen Ground-Truth Baseline: Spatial Engine 2.0 (Commit e153da5)

This directory contains the authentic, frozen reference implementation of the Dimension5 DSP Core from commit:

```
Commit:  e153da53c7b4efe07eac2a7e814f046973f3b4a9
Subject: Merge branch 'fix/spatial-engine-final-ipc-consistency' into main
Date:    Phase 1.2.3 Release Baseline
```

## Purpose

This reference is strictly preserved to satisfy **Priority 4 / Section 9** of the Phase A.1 specification:
- Ensure true regression testing against the actual prior version (`e153da5`), NOT against another instance of the new BBD Character DSP.
- Verify bit-exact and floating-point equivalence when `bbdCharacter == 0.0f` (BBD Character OFF) across Modes I..IV and Custom.
- Symbols in `dimension_dsp_e153da5.h` and `dimension_dsp_e153da5.c` are prefixed with `_e153da5` to avoid symbol collisions when linked alongside the current DSP core in test harnesses.
