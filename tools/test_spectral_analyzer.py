#!/usr/bin/env python3
"""
DIMENSION5 — BBD CHARACTER ENGINE 2.1
Negative Test Harness for Spectral & Invariance Validator
Phase A.2 Acceptance Suite

Proves that the validation tooling reliably catches deliberate acoustic and temporal defects:
1. Known spurious sinusoidal component outside modulation zone.
2. Known folded/aliased spectral component (simulated Nyquist reflection).
3. Artificial block-boundary sample jump / click discontinuity.
4. Automation event temporal displacement (time-shifted parameter change).
5. Known gain/level mismatch between compared passes.
"""

import sys
import numpy as np
from analyze_spectral_validation import analyze_carrier_and_sidebands, compare_block_invariance

def run_negative_tests():
    print("=" * 72)
    print("RUNNING NEGATIVE TESTS ON SPECTRAL & INVARIANCE VALIDATOR")
    print("=" * 72)
    
    sr = 48000
    duration_sec = 16.0
    n = int(sr * duration_sec)
    t = np.arange(n, dtype=np.float32) / float(sr)
    
    # Baseline clean signal: 1000 Hz carrier with 0.25 Hz LFO phase modulation
    # mimicking Dimension Chorus sideband structure
    f_c = 1000.0
    f_m = 0.25
    depth = 0.0010 # 1.0 ms modulation
    lfo = np.sin(2.0 * np.pi * f_m * t)
    clean_phase = 2.0 * np.pi * f_c * (t + depth * lfo)
    clean_signal = 0.5 * np.sin(clean_phase).astype(np.float32)
    
    passed_tests = 0
    total_tests = 5
    
    # -------------------------------------------------------------------------
    # TEST 1: Spurious Sinusoid Injection
    # Injected spurious tone at 450 Hz (-35 dBc)
    # -------------------------------------------------------------------------
    print("\n[TEST 1] Injected Spurious Sinusoid at 450 Hz (-35 dBc)...")
    spur_signal = clean_signal.copy()
    spur_amplitude = 0.5 * (10.0 ** (-35.0 / 20.0))
    spur_signal += (spur_amplitude * np.sin(2.0 * np.pi * 450.0 * t)).astype(np.float32)
    
    res_clean = analyze_carrier_and_sidebands(clean_signal, sr, f_carrier=f_c, f_mod=f_m)
    res_spur = analyze_carrier_and_sidebands(spur_signal, sr, f_carrier=f_c, f_mod=f_m)
    
    # Criteria: max spurious frequency should identify 450 Hz with level ~ -35 dBc, SFDR <= 40 dB
    detected_spur = (abs(res_spur["max_spurious_freq_hz"] - 450.0) < 1.0 and
                     res_spur["sfdr_db"] < 40.0)
    if detected_spur:
        print(f"  --> PASS: Spurious component detected at {res_spur['max_spurious_freq_hz']:.2f} Hz "
              f"with level {res_spur['max_spurious_dbc']:.1f} dBc (SFDR={res_spur['sfdr_db']:.1f} dB)")
        passed_tests += 1
    else:
        print(f"  --> FAIL: Spurious component at 450 Hz was NOT caught! (Detected at {res_spur['max_spurious_freq_hz']:.1f} Hz, SFDR={res_spur['sfdr_db']:.1f} dB)")

    # -------------------------------------------------------------------------
    # TEST 2: Reflected Aliasing Injection
    # 15 kHz carrier at 44.1 kHz sr -> 3rd harmonic (45 kHz) folds to 900 Hz
    # We inject simulated reflected aliasing at 900 Hz (-45 dBc)
    # -------------------------------------------------------------------------
    print("\n[TEST 2] Injected Reflected Foldover Aliasing at 900 Hz...")
    sr_44 = 44100
    n_44 = int(sr_44 * duration_sec)
    t_44 = np.arange(n_44, dtype=np.float32) / float(sr_44)
    sig_15k = (0.5 * np.sin(2.0 * np.pi * 15000.0 * (t_44 + depth * np.sin(2.0 * np.pi * f_m * t_44)))).astype(np.float32)
    
    # Injected alias component
    alias_amp = 0.5 * (10.0 ** (-45.0 / 20.0))
    sig_15k_aliased = sig_15k + (alias_amp * np.sin(2.0 * np.pi * 900.0 * t_44)).astype(np.float32)
    
    res_alias = analyze_carrier_and_sidebands(sig_15k_aliased, sr_44, f_carrier=15000.0, f_mod=f_m)
    alias_detected = False
    for cand in res_alias["aliasing_candidates"]:
        if abs(cand["measured_freq_hz"] - 900.0) < 5.0 and cand["is_significant"]:
            alias_detected = True
            break
            
    if alias_detected:
        print(f"  --> PASS: Folded aliasing candidate at 900 Hz identified and flagged as significant! "
              f"Level={cand['rel_dbc']:.1f} dBc")
        passed_tests += 1
    else:
        print(f"  --> FAIL: Folded aliasing component was NOT caught!")

    # -------------------------------------------------------------------------
    # TEST 3: Block-Boundary Sample Jump / Click Discontinuity
    # Inject 0.05 step jump across sample 64 (block boundary)
    # -------------------------------------------------------------------------
    print("\n[TEST 3] Artificial Block-Boundary Discontinuity (+0.05 step jump at sample 64)...")
    sig_jump = clean_signal.copy()
    sig_jump[64:] += 0.05
    
    comp_jump = compare_block_invariance(clean_signal, sig_jump, sr, "boundary_jump")
    if comp_jump["max_single_sample_jump"] >= 0.045:
        print(f"  --> PASS: Boundary jump caught! Max jump={comp_jump['max_single_sample_jump']:.5f} "
              f"(threshold=0.010)")
        passed_tests += 1
    else:
        print(f"  --> FAIL: Boundary jump missed! Max jump={comp_jump['max_single_sample_jump']:.5f}")

    # -------------------------------------------------------------------------
    # TEST 4: Automation Timing Displacement (Time-Shifted Event)
    # Event occurs at sample 520 instead of sample 512 (8-sample lag)
    # -------------------------------------------------------------------------
    print("\n[TEST 4] Automation Timing Displacement (8-sample shift)...")
    ramp_ref = clean_signal.copy()
    ramp_ref[512:1536] *= 1.5 # 50% gain increase at sample 512
    
    ramp_shifted = clean_signal.copy()
    ramp_shifted[520:1536] *= 1.5 # Delayed by 8 samples
    
    comp_shift = compare_block_invariance(ramp_ref, ramp_shifted, sr, "timing_displacement")
    # For temporal invariance, max_abs_diff detects the sample error immediately (> 0.05)
    if comp_shift["max_abs_diff"] > 0.05:
        print(f"  --> PASS: Automation timing lag detected! MaxAbsDiff={comp_shift['max_abs_diff']:.5f} "
              f"(threshold=0.010, bit_exact={comp_shift['is_bit_exact']})")
        passed_tests += 1
    else:
        print(f"  --> FAIL: Automation timing lag was NOT caught! MaxDiff={comp_shift['max_abs_diff']:.5f}")

    # -------------------------------------------------------------------------
    # TEST 5: Known Gain Mismatch (+0.5 dB offset)
    # -------------------------------------------------------------------------
    print("\n[TEST 5] Known Gain Mismatch (+0.5 dB offset)...")
    gain_factor = 10.0 ** (0.5 / 20.0) # ~1.05925
    sig_gained = clean_signal * gain_factor
    
    comp_gain = compare_block_invariance(clean_signal, sig_gained, sr, "gain_mismatch")
    if comp_gain["max_abs_diff"] > 0.02 and abs(comp_gain["nrmse"] - (gain_factor - 1.0)) < 0.005:
        print(f"  --> PASS: Gain mismatch (+0.5 dB) detected! NRMSE={comp_gain['nrmse']:.5f} "
              f"(expected ~{gain_factor - 1.0:.5f})")
        passed_tests += 1
    else:
        print(f"  --> FAIL: Gain mismatch was NOT caught! NRMSE={comp_gain['nrmse']:.5f}")

    print("\n" + "=" * 72)
    print(f"NEGATIVE TEST SUITE RESULT: {passed_tests} / {total_tests} TESTS PASSED")
    print("=" * 72)
    
    return 0 if (passed_tests == total_tests) else 1

if __name__ == '__main__':
    sys.exit(run_negative_tests())
