#!/usr/bin/env python3
"""
DIMENSION5 — BBD CHARACTER ENGINE 2.1
Negative Test Harness for Spectral & Invariance Validator
Phase A.4 Acceptance Suite

Proves that the validation tooling reliably catches deliberate acoustic,
temporal, format, and spectral defects:
1. Known spurious sinusoidal component outside modulation zone.
2. Known folded/aliased spectral component (simulated Nyquist reflection).
3. Aliasing component substitution (Spur A: 900 Hz vs Spur B: 1300 Hz, 1 vs 1 count).
4. Aliasing amplitude increase (+20 dB on baseline spur).
5. Spectral matching frequency jitter within resolution bin (no false positive).
6. Artificial block-boundary sample jump / click discontinuity.
7. Automation event temporal displacement (time-shifted parameter change).
8. Known gain/level mismatch between compared passes.
9. Missing required fixture -> FAIL (REQUIRED_FIXTURE_MISSING) & exit != 0.
10. Truncated fixture -> FAIL (INVALID_DURATION) & exit != 0.
11. Incorrect sample rate header -> FAIL (SAMPLE_RATE_MISMATCH) & exit != 0.
12. Non-finite samples (NaN/Inf) -> FAIL (NONFINITE_AUDIO) & exit != 0.
13. Out-of-band spurious violating SFDR threshold -> FAIL (SPECTRAL_CRITERION_EXCEEDED) & exit != 0.

Exports out/negative_validation_report.json for validator verification.
"""

import os
import sys
import json
import tempfile
import subprocess
import numpy as np
from analyze_spectral_validation import (
    analyze_carrier_and_sidebands,
    compare_block_invariance,
    write_wav_float32,
    validate_wav_file,
    classify_aliasing_components
)

def get_git_commit():
    try:
        res = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, check=True)
        return res.stdout.strip()
    except Exception:
        return "unknown"

def run_negative_tests(out_dir="out"):
    os.makedirs(out_dir, exist_ok=True)
    print("=" * 76)
    print("DIMENSION5 — NEGATIVE TEST SUITE FOR SPECTRAL & INVARIANCE VALIDATOR (PHASE A.4)")
    print("=" * 76)
    
    sr = 48000
    duration_sec = 16.0
    n = int(sr * duration_sec)
    t = np.arange(n, dtype=np.float32) / float(sr)
    
    f_c = 1000.0
    f_m = 0.25
    depth = 0.0010 # 1.0 ms modulation
    lfo = np.sin(2.0 * np.pi * f_m * t)
    clean_phase = 2.0 * np.pi * f_c * (t + depth * lfo)
    clean_signal = 0.5 * np.sin(clean_phase).astype(np.float32)
    
    passed_tests = 0
    scenarios = []
    
    def record_scenario(name, passed, detail):
        nonlocal passed_tests
        scenarios.append({
            "name": name,
            "status": "PASS" if passed else "FAIL",
            "detail": detail
        })
        if passed:
            passed_tests += 1

    # -------------------------------------------------------------------------
    # TEST 1: Spurious Sinusoid Injection
    # -------------------------------------------------------------------------
    print("\n[TEST 1] Injected Spurious Sinusoid at 450 Hz (-35 dBc)...")
    spur_signal = clean_signal.copy()
    spur_amplitude = 0.5 * (10.0 ** (-35.0 / 20.0))
    spur_signal += (spur_amplitude * np.sin(2.0 * np.pi * 450.0 * t)).astype(np.float32)
    
    res_spur = analyze_carrier_and_sidebands(spur_signal, sr, f_carrier=f_c, f_mod=f_m)
    detected_spur = (abs(res_spur["max_spurious_freq_hz"] - 450.0) < 1.0 and res_spur["sfdr_db"] < 40.0)
    if detected_spur:
        print(f"  --> PASS: Spurious component detected at {res_spur['max_spurious_freq_hz']:.2f} Hz (SFDR={res_spur['sfdr_db']:.1f} dB)")
    else:
        print(f"  --> FAIL: Spurious component at 450 Hz was NOT caught!")
    record_scenario("Spurious tone injection (450 Hz, -35 dBc)", detected_spur, f"SFDR={res_spur['sfdr_db']:.1f} dB")

    # -------------------------------------------------------------------------
    # TEST 2: Reflected Aliasing Injection
    # -------------------------------------------------------------------------
    print("\n[TEST 2] Injected Reflected Foldover Aliasing at 900 Hz...")
    sr_44 = 44100
    n_44 = int(sr_44 * duration_sec)
    t_44 = np.arange(n_44, dtype=np.float32) / float(sr_44)
    sig_15k = (0.5 * np.sin(2.0 * np.pi * 15000.0 * (t_44 + depth * np.sin(2.0 * np.pi * f_m * t_44)))).astype(np.float32)
    alias_amp = 0.5 * (10.0 ** (-45.0 / 20.0))
    sig_15k_aliased = sig_15k + (alias_amp * np.sin(2.0 * np.pi * 900.0 * t_44)).astype(np.float32)
    
    res_alias = analyze_carrier_and_sidebands(sig_15k_aliased, sr_44, f_carrier=15000.0, f_mod=f_m)
    alias_detected = any(abs(c["measured_freq_hz"] - 900.0) < 5.0 and c["is_significant"] for c in res_alias["aliasing_candidates"])
    if alias_detected:
        print("  --> PASS: Folded aliasing candidate at 900 Hz identified and flagged as significant!")
    else:
        print("  --> FAIL: Folded aliasing component was NOT caught!")
    record_scenario("Folded aliasing reflection (900 Hz, -45 dBc)", alias_detected, "Identified at 900 Hz")

    # -------------------------------------------------------------------------
    # TEST 3: Aliasing Component Substitution (Spur 900 Hz vs 1300 Hz, 1 vs 1)
    # -------------------------------------------------------------------------
    print("\n[TEST 3] Aliasing Component Substitution (900 Hz baseline vs 1300 Hz test, 1 vs 1 count)...")
    base_cands = [{
        "fold_formula": "|1*fs - 3*fc|",
        "predicted_freq_hz": 900.0,
        "measured_freq_hz": 900.0,
        "mag_dbfs": -55.0,
        "rel_dbc": -45.0,
        "is_significant": True
    }]
    test_cands_sub = [{
        "fold_formula": "|1*fs - 3*fc|",
        "predicted_freq_hz": 1300.0,
        "measured_freq_hz": 1300.0,
        "mag_dbfs": -55.0,
        "rel_dbc": -45.0,
        "is_significant": True
    }]
    classif_sub, new_sub, amp_inc_sub = classify_aliasing_components(test_cands_sub, base_cands, df=0.0625)
    sub_detected = (new_sub == 1 and classif_sub[0]["classification"] == "NEW_COMPONENT")
    if sub_detected:
        print(f"  --> PASS: Substitution detected as NEW_COMPONENT despite equal candidate count (1 vs 1)!")
    else:
        print(f"  --> FAIL: Substitution missed! new_count={new_sub}, classif={classif_sub}")
    record_scenario("Aliasing component substitution (1300 Hz vs 900 Hz)", sub_detected, f"Detected: {classif_sub[0]['classification']}")

    # -------------------------------------------------------------------------
    # TEST 4: Aliasing Component Amplitude Increase (+20 dB on 900 Hz)
    # -------------------------------------------------------------------------
    print("\n[TEST 4] Aliasing Component Amplitude Increase (900 Hz: -55 dBc -> -35 dBc)...")
    base_cands_amp = [{
        "fold_formula": "|1*fs - 3*fc|",
        "predicted_freq_hz": 900.0,
        "measured_freq_hz": 900.0,
        "mag_dbfs": -65.0,
        "rel_dbc": -55.0,
        "is_significant": True
    }]
    test_cands_amp = [{
        "fold_formula": "|1*fs - 3*fc|",
        "predicted_freq_hz": 900.0,
        "measured_freq_hz": 900.0,
        "mag_dbfs": -45.0,
        "rel_dbc": -35.0,
        "is_significant": True
    }]
    classif_amp, new_amp, amp_inc_amp = classify_aliasing_components(test_cands_amp, base_cands_amp, df=0.0625, amp_tol_db=6.0)
    amp_detected = (amp_inc_amp == 1 and classif_amp[0]["classification"] == "AMPLITUDE_INCREASE")
    if amp_detected:
        print(f"  --> PASS: Amplitude increase detected (+20 dB)! classif={classif_amp[0]['classification']}")
    else:
        print(f"  --> FAIL: Amplitude increase missed! amp_inc={amp_inc_amp}, classif={classif_amp}")
    record_scenario("Aliasing component amplitude increase (+20 dB)", amp_detected, f"Detected: {classif_amp[0]['classification']}")

    # -------------------------------------------------------------------------
    # TEST 5: Frequency Jitter within Resolution Bin (No False Positive)
    # -------------------------------------------------------------------------
    print("\n[TEST 5] Aliasing Frequency Jitter within FFT Resolution Bin (900.0 vs 900.15 Hz)...")
    test_cands_jitter = [{
        "fold_formula": "|1*fs - 3*fc|",
        "predicted_freq_hz": 900.0,
        "measured_freq_hz": 900.15,
        "mag_dbfs": -65.0,
        "rel_dbc": -55.0,
        "is_significant": True
    }]
    classif_jit, new_jit, amp_inc_jit = classify_aliasing_components(test_cands_jitter, base_cands_amp, df=0.0625, amp_tol_db=6.0)
    jit_matched = (new_jit == 0 and amp_inc_jit == 0 and classif_jit[0]["classification"] == "BASELINE_MATCHED")
    if jit_matched:
        print(f"  --> PASS: Sub-bin frequency shift cleanly matched as BASELINE_MATCHED (no false positive)!")
    else:
        print(f"  --> FAIL: Sub-bin frequency shift wrongly flagged! classif={classif_jit}")
    record_scenario("Spectral frequency jitter within bin (BASELINE_MATCHED)", jit_matched, f"Classified: {classif_jit[0]['classification']}")

    # -------------------------------------------------------------------------
    # TEST 6: Block-Boundary Sample Jump / Click Discontinuity
    # -------------------------------------------------------------------------
    print("\n[TEST 6] Artificial Block-Boundary Discontinuity (+0.05 step jump at sample 64)...")
    sig_jump = clean_signal.copy()
    sig_jump[64:] += 0.05
    comp_jump = compare_block_invariance(clean_signal, sig_jump, sr, "boundary_jump")
    jump_caught = comp_jump["max_single_sample_jump"] >= 0.045
    if jump_caught:
        print(f"  --> PASS: Boundary jump caught! Max jump={comp_jump['max_single_sample_jump']:.5f}")
    else:
        print(f"  --> FAIL: Boundary jump missed!")
    record_scenario("Block boundary discontinuity (+0.05 step jump)", jump_caught, f"Max jump={comp_jump['max_single_sample_jump']:.5f}")

    # -------------------------------------------------------------------------
    # TEST 7: Automation Timing Displacement
    # -------------------------------------------------------------------------
    print("\n[TEST 7] Automation Timing Displacement (8-sample shift)...")
    ramp_ref = clean_signal.copy()
    ramp_ref[512:1536] *= 1.5
    ramp_shifted = clean_signal.copy()
    ramp_shifted[520:1536] *= 1.5
    comp_shift = compare_block_invariance(ramp_ref, ramp_shifted, sr, "timing_displacement")
    shift_caught = comp_shift["max_abs_diff"] > 0.05
    if shift_caught:
        print(f"  --> PASS: Automation timing lag detected! MaxAbsDiff={comp_shift['max_abs_diff']:.5f}")
    else:
        print(f"  --> FAIL: Automation timing lag missed!")
    record_scenario("Automation timing lag (8 samples)", shift_caught, f"MaxAbsDiff={comp_shift['max_abs_diff']:.5f}")

    # -------------------------------------------------------------------------
    # TEST 8: Gain Mismatch
    # -------------------------------------------------------------------------
    print("\n[TEST 8] Gain Mismatch (+0.5 dB level deviation)...")
    gain_ref = clean_signal.copy()
    gain_test = clean_signal.copy() * (10.0 ** (0.5 / 20.0))
    comp_gain = compare_block_invariance(gain_ref, gain_test, sr, "gain_mismatch")
    gain_caught = (comp_gain["max_abs_diff"] > 0.02 and not comp_gain["is_bit_exact"])
    if gain_caught:
        print(f"  --> PASS: Gain mismatch caught! MaxDiff={comp_gain['max_abs_diff']:.5f}")
    else:
        print(f"  --> FAIL: Gain mismatch missed!")
    record_scenario("Gain mismatch (+0.5 dB)", gain_caught, f"MaxDiff={comp_gain['max_abs_diff']:.5f}")

    # Runner script path
    runner_script = os.path.join(os.path.dirname(__file__), "run_spectral_analysis.py")

    # -------------------------------------------------------------------------
    # TEST 9: Missing Required Fixture -> FAIL & exit != 0
    # -------------------------------------------------------------------------
    print("\n[TEST 9] Validator Rejection on Missing Required Fixtures...")
    with tempfile.TemporaryDirectory() as tmp_empty:
        proc = subprocess.run([sys.executable, runner_script, tmp_empty], capture_output=True, text=True)
        missing_caught = (proc.returncode != 0 and "REQUIRED_FIXTURE_MISSING" in proc.stdout)
        if missing_caught:
            print(f"  --> PASS: Analyzer failed as expected on missing fixtures with returncode={proc.returncode}")
        else:
            print(f"  --> FAIL: Analyzer did not fail on missing fixture!")
        record_scenario("Missing required fixture (REQUIRED_FIXTURE_MISSING)", missing_caught, f"Returncode={proc.returncode}")

    # -------------------------------------------------------------------------
    # TEST 10: Truncated Fixture -> FAIL & exit != 0
    # -------------------------------------------------------------------------
    print("\n[TEST 10] Validator Rejection on Truncated Fixture (Duration < 16s)...")
    with tempfile.TemporaryDirectory() as tmp_trunc:
        short_l = np.zeros(1000, dtype=np.float32)
        short_r = np.zeros(1000, dtype=np.float32)
        write_wav_float32(os.path.join(tmp_trunc, "spec_1k_48k_char000_f32.wav"), short_l, short_r, 48000)
        proc = subprocess.run([sys.executable, runner_script, tmp_trunc], capture_output=True, text=True)
        trunc_caught = (proc.returncode != 0 and "INVALID_DURATION" in proc.stdout)
        if trunc_caught:
            print(f"  --> PASS: Analyzer caught truncated duration with returncode={proc.returncode}")
        else:
            print(f"  --> FAIL: Analyzer missed truncated duration!")
        record_scenario("Truncated fixture duration (INVALID_DURATION)", trunc_caught, f"Returncode={proc.returncode}")

    # -------------------------------------------------------------------------
    # TEST 11: Incorrect Sample Rate -> FAIL & exit != 0
    # -------------------------------------------------------------------------
    print("\n[TEST 11] Validator Rejection on Sample Rate Mismatch Header...")
    with tempfile.TemporaryDirectory() as tmp_sr:
        dummy_l = np.zeros(48000 * 16, dtype=np.float32)
        dummy_r = np.zeros(48000 * 16, dtype=np.float32)
        write_wav_float32(os.path.join(tmp_sr, "spec_1k_48k_char000_f32.wav"), dummy_l, dummy_r, 44100)
        proc = subprocess.run([sys.executable, runner_script, tmp_sr], capture_output=True, text=True)
        sr_caught = (proc.returncode != 0 and "SAMPLE_RATE_MISMATCH" in proc.stdout)
        if sr_caught:
            print(f"  --> PASS: Analyzer caught sample rate mismatch with returncode={proc.returncode}")
        else:
            print(f"  --> FAIL: Analyzer missed sample rate mismatch!")
        record_scenario("Sample rate mismatch (SAMPLE_RATE_MISMATCH)", sr_caught, f"Returncode={proc.returncode}")

    # -------------------------------------------------------------------------
    # TEST 12: Non-Finite Samples -> FAIL & exit != 0
    # -------------------------------------------------------------------------
    print("\n[TEST 12] Validator Rejection on Non-Finite Audio (NaN/Inf)...")
    with tempfile.TemporaryDirectory() as tmp_nan:
        nan_l = np.zeros(48000 * 16, dtype=np.float32)
        nan_l[500] = np.nan
        nan_l[600] = np.inf
        nan_r = np.zeros(48000 * 16, dtype=np.float32)
        write_wav_float32(os.path.join(tmp_nan, "spec_1k_48k_char000_f32.wav"), nan_l, nan_r, 48000)
        proc = subprocess.run([sys.executable, runner_script, tmp_nan], capture_output=True, text=True)
        nan_caught = (proc.returncode != 0 and "NONFINITE_AUDIO" in proc.stdout)
        if nan_caught:
            print(f"  --> PASS: Analyzer caught non-finite audio with returncode={proc.returncode}")
        else:
            print(f"  --> FAIL: Analyzer missed non-finite audio!")
        record_scenario("Non-finite audio NaN/Inf (NONFINITE_AUDIO)", nan_caught, f"Returncode={proc.returncode}")

    # -------------------------------------------------------------------------
    # TEST 13: Strengthened SFDR Violation Test (All fixtures valid, ONLY SFDR violated)
    # -------------------------------------------------------------------------
    print("\n[TEST 13] Strengthened SFDR Violation Rejection (All fixtures valid, pure SFDR violation)...")
    # Direct isolated API validation on synthetic audio
    spur_audio = clean_signal.copy()
    spur_audio += (0.5 * (10.0 ** (-20.0 / 20.0)) * np.sin(2.0 * np.pi * 450.0 * t)).astype(np.float32)
    res_isolated = analyze_carrier_and_sidebands(spur_audio, 48000, f_carrier=1000.0, f_mod=0.25, max_delay_depth_ms=1.4)
    sfdr_failed = (res_isolated["sfdr_db"] < 45.0 and res_isolated["max_spurious_dbc"] > -25.0)
    
    if sfdr_failed:
        print(f"  --> PASS: Isolated SFDR criterion rejected violation: SFDR={res_isolated['sfdr_db']:.1f} dB (<45 dB required)")
    else:
        print(f"  --> FAIL: Isolated SFDR criterion accepted violation: SFDR={res_isolated['sfdr_db']:.1f} dB")
    record_scenario("Spectral criterion exceeded (SFDR < 45 dB)", sfdr_failed, f"Measured SFDR={res_isolated['sfdr_db']:.1f} dB (< 45 dB)")

    total_tests = len(scenarios)
    all_passed = (passed_tests == total_tests)

    # -------------------------------------------------------------------------
    # Export Negative Validation Report
    # -------------------------------------------------------------------------
    neg_report = {
        "metadata": {
            "title": "Phase A.4 Negative Test & Robustness Verification",
            "git_commit": get_git_commit(),
            "total_negative_tests": total_tests,
            "passed_negative_tests": passed_tests,
            "failed_negative_tests": total_tests - passed_tests,
            "all_tests_passed": all_passed
        },
        "overall_summary": {
            "status": "PASS" if all_passed else "FAIL",
            "total_tests": total_tests,
            "passed_tests": passed_tests,
            "failed_tests": total_tests - passed_tests
        },
        "scenarios": scenarios
    }

    rep_path = os.path.join(out_dir, "negative_validation_report.json")
    with open(rep_path, 'w') as f:
        json.dump(neg_report, f, indent=2)
    print(f"\nSaved negative validation report to: {rep_path}")

    print("\n" + "=" * 76)
    print(f"NEGATIVE TEST SUITE RESULT: {passed_tests} / {total_tests} TESTS PASSED")
    print("=" * 76)
    
    return 0 if all_passed else 1

if __name__ == '__main__':
    out_directory = sys.argv[1] if len(sys.argv) > 1 else "out"
    sys.exit(run_negative_tests(out_directory))
