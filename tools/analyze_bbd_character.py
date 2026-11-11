#!/usr/bin/env python3
"""
DIMENSION5 — BBD CHARACTER ENGINE 2.1
Audio Characterization & Neutrality Analysis Harness
Phase A.1 Validation Suite
"""

import math
import struct
import wave
import json
import os
import sys

def read_wav(path):
    with wave.open(path, 'rb') as w:
        n_channels = w.getnchannels()
        sr = w.getframerate()
        n_frames = w.getnframes()
        data = w.readframes(n_frames)
        fmt = f"<{n_frames * n_channels}h"
        raw = struct.unpack(fmt, data)
        scale = 1.0 / 32768.0
        
        left = [raw[i * n_channels] * scale for i in range(n_frames)]
        right = [raw[i * n_channels + 1] * scale for i in range(n_frames)] if n_channels > 1 else list(left)
        return left, right, sr

def compute_metrics(samples):
    n = len(samples)
    if n == 0:
        return 0.0, 0.0, 0.0
    peak = max(abs(x) for x in samples)
    dc = sum(samples) / n
    rms = math.sqrt(sum(x * x for x in samples) / n)
    return peak, dc, rms

def compute_stereo_metrics(left, right):
    n = min(len(left), len(right))
    if n == 0:
        return {}
    
    mean_l = sum(left[:n]) / n
    mean_r = sum(right[:n]) / n
    
    diff_lr_sq = sum((left[i] - right[i]) ** 2 for i in range(n))
    rms_diff_lr = math.sqrt(diff_lr_sq / n)
    
    cov = sum((left[i] - mean_l) * (right[i] - mean_r) for i in range(n))
    var_l = sum((left[i] - mean_l) ** 2 for i in range(n))
    var_r = sum((right[i] - mean_r) ** 2 for i in range(n))
    denom = math.sqrt(var_l * var_r)
    correlation = (cov / denom) if denom > 1e-12 else 1.0
    
    mid_energy = sum((0.5 * (left[i] + right[i])) ** 2 for i in range(n))
    side_energy = sum((0.5 * (left[i] - right[i])) ** 2 for i in range(n))
    side_to_mid_ratio = (side_energy / mid_energy) if mid_energy > 1e-12 else 0.0
    
    return {
        "rms_diff_lr": rms_diff_lr,
        "stereo_correlation": correlation,
        "mid_energy": mid_energy / n,
        "side_energy": side_energy / n,
        "side_to_mid_ratio": side_to_mid_ratio
    }

def compute_spectral_indicators(samples, sr, f_carrier=220.0):
    n = len(samples)
    # Take middle 4096 samples for steady state analysis
    start = max(0, n // 2 - 2048)
    end = start + 4096
    if end > n:
        return {}
    
    segment = samples[start:end]
    seg_len = len(segment)
    
    # Hann window
    windowed = [segment[i] * 0.5 * (1.0 - math.cos(2.0 * math.pi * i / seg_len)) for i in range(seg_len)]
    
    # Evaluate discrete Fourier bins
    def dft_power_at(freq):
        real = sum(windowed[i] * math.cos(2.0 * math.pi * freq * i / sr) for i in range(seg_len))
        imag = sum(-windowed[i] * math.sin(2.0 * math.pi * freq * i / sr) for i in range(seg_len))
        return (real * real + imag * imag) / (seg_len * seg_len)
    
    p_carrier = dft_power_at(f_carrier)
    p_side_plus = dft_power_at(f_carrier + 0.25)
    p_side_minus = dft_power_at(f_carrier - 0.25)
    p_high = dft_power_at(10000.0)
    p_nyquist_margin = dft_power_at(0.48 * sr)
    
    total_energy = sum(x * x for x in windowed) / seg_len
    
    return {
        "carrier_power": p_carrier,
        "sideband_power_rel_db": 10.0 * math.log10(max(1e-12, (p_side_plus + p_side_minus) / max(1e-12, p_carrier))),
        "hf_10k_power_db": 10.0 * math.log10(max(1e-12, p_high / max(1e-12, total_energy))),
        "aliasing_margin_power_db": 10.0 * math.log10(max(1e-12, p_nyquist_margin / max(1e-12, total_energy)))
    }

def compare_signals(ref_l, ref_r, test_l, test_r):
    n = min(len(ref_l), len(test_l))
    max_diff_l = 0.0
    max_diff_r = 0.0
    sum_sq_diff_l = 0.0
    sum_sq_diff_r = 0.0
    divergent_samples = 0
    
    for i in range(n):
        dl = abs(test_l[i] - ref_l[i])
        dr = abs(test_r[i] - ref_r[i])
        if dl > max_diff_l:
            max_diff_l = dl
        if dr > max_diff_r:
            max_diff_r = dr
        sum_sq_diff_l += dl * dl
        sum_sq_diff_r += dr * dr
        if dl > 1e-5 or dr > 1e-5:
            divergent_samples += 1
            
    max_diff = max(max_diff_l, max_diff_r)
    rms_diff_l = math.sqrt(sum_sq_diff_l / n)
    rms_diff_r = math.sqrt(sum_sq_diff_r / n)
    rms_diff = math.sqrt(0.5 * (rms_diff_l * rms_diff_l + rms_diff_r * rms_diff_r))
    bit_exact = (max_diff == 0.0)
    
    return {
        "max_diff_overall": max_diff,
        "max_diff_left": max_diff_l,
        "max_diff_right": max_diff_r,
        "rms_diff_overall": rms_diff,
        "rms_diff_left": rms_diff_l,
        "rms_diff_right": rms_diff_r,
        "divergent_samples": divergent_samples,
        "total_samples": n,
        "is_bit_exact": bit_exact
    }

def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "out"
    print("=" * 72)
    print("DIMENSION5 — BBD CHARACTER ENGINE 2.1 AUDIO CHARACTERIZATION")
    print("=" * 72)

    results = {}

    # 1. Ground-Truth Neutrality Against Spatial Engine 2.0 Baseline (e153da5)
    base_path = os.path.join(out_dir, "spatial_engine_2_0_baseline.wav")
    zero_path = os.path.join(out_dir, "bbd_character_000.wav")
    
    if os.path.exists(base_path) and os.path.exists(zero_path):
        base_l, base_r, sr = read_wav(base_path)
        zero_l, zero_r, _ = read_wav(zero_path)
        comp_baseline = compare_signals(base_l, base_r, zero_l, zero_r)
        results["neutrality_vs_e153da5"] = comp_baseline
        print("\n--- 1. GROUND-TRUTH NEUTRALITY (e153da5 Baseline vs bbdCharacter = 0%) ---")
        print(f"  Bit-Exact Match : {'YES (PERFECT PASS)' if comp_baseline['is_bit_exact'] else 'NO'}")
        print(f"  Max Absolute Diff: {comp_baseline['max_diff_overall']:.8f}")
        print(f"  RMS Diff         : {comp_baseline['rms_diff_overall']:.8f}")
        print(f"  Divergent Samples: {comp_baseline['divergent_samples']} / {comp_baseline['total_samples']}")
    else:
        print(f"Warning: Baseline or 0% file not found in {out_dir}")

    # 2. Reference 0% Analysis
    ref_l, ref_r, sr = read_wav(zero_path)
    ref_peak_l, ref_dc_l, ref_rms_l = compute_metrics(ref_l)
    ref_peak_r, ref_dc_r, ref_rms_r = compute_metrics(ref_r)
    ref_stereo = compute_stereo_metrics(ref_l, ref_r)
    ref_spectral = compute_spectral_indicators(ref_l, sr)

    print(f"\n--- 2. REFERENCE CHARACTERISTICS (bbd_character_000.wav) ---")
    print(f"  Left Channel  : Peak={ref_peak_l:.4f}, DC={ref_dc_l:.6f}, RMS={ref_rms_l:.4f}")
    print(f"  Right Channel : Peak={ref_peak_r:.4f}, DC={ref_dc_r:.6f}, RMS={ref_rms_r:.4f}")
    print(f"  Stereo Metric : Correlation={ref_stereo['stereo_correlation']:.4f}, Side/Mid={ref_stereo['side_to_mid_ratio']:.4f}")
    print(f"  Sideband Pwr  : {ref_spectral.get('sideband_power_rel_db', 0.0):.1f} dB rel carrier")

    # 3. Progressive Character Progression
    levels = ["025", "050", "075", "100"]
    print("\n--- 3. PROGRESSIVE BBD CHARACTER SCALING (Full Mix) ---")
    for lvl in levels:
        test_path = os.path.join(out_dir, f"bbd_character_{lvl}.wav")
        test_l, test_r, _ = read_wav(test_path)
        comp = compare_signals(ref_l, ref_r, test_l, test_r)
        st = compute_stereo_metrics(test_l, test_r)
        sp = compute_spectral_indicators(test_l, sr)
        comp["stereo_metrics"] = st
        comp["spectral_indicators"] = sp
        results[f"bbd_character_{lvl}"] = comp
        print(f"BBD Character {int(lvl)}% vs 0%:")
        print(f"  Max Diff: {comp['max_diff_overall']:.6f} | RMS Diff: {comp['rms_diff_overall']:.6f}")
        print(f"  Stereo Correlation: {st['stereo_correlation']:.4f} | Side/Mid Ratio: {st['side_to_mid_ratio']:.4f}")
        print(f"  HF Power (10k): {sp['hf_10k_power_db']:.1f} dB | Aliasing Margin: {sp['aliasing_margin_power_db']:.1f} dB")

    # 4. Wet-Only Progression
    print("\n--- 4. WET-ONLY PROGRESSION ---")
    wet_ref_path = os.path.join(out_dir, "bbd_character_000_wet.wav")
    wet_ref_l, wet_ref_r, _ = read_wav(wet_ref_path)
    for lvl in ["050", "100"]:
        wet_test_path = os.path.join(out_dir, f"bbd_character_{lvl}_wet.wav")
        wet_l, wet_r, _ = read_wav(wet_test_path)
        comp = compare_signals(wet_ref_l, wet_ref_r, wet_l, wet_r)
        results[f"bbd_character_{lvl}_wet"] = comp
        print(f"Wet-Only BBD Character {int(lvl)}% vs 0%:")
        print(f"  Max Diff: {comp['max_diff_overall']:.6f} | RMS Diff: {comp['rms_diff_overall']:.6f}")

    # 5. Multitone & Impulses Fixtures
    print("\n--- 5. MULTITONE & IMPULSES FIXTURES ---")
    for name in ["bbd_multitone", "bbd_impulses"]:
        p0 = os.path.join(out_dir, f"{name}_000.wav")
        p1 = os.path.join(out_dir, f"{name}_100.wav")
        l0, r0, _ = read_wav(p0)
        l1, r1, _ = read_wav(p1)
        comp = compare_signals(l0, r0, l1, r1)
        results[f"{name}_100_vs_000"] = comp
        print(f"{name} 100% vs 0%:")
        print(f"  Max Diff: {comp['max_diff_overall']:.6f} | RMS Diff: {comp['rms_diff_overall']:.6f}")

    # 6. Monotonicity Evaluation & Documentation Note
    diff_025 = results["bbd_character_025"]["rms_diff_overall"]
    diff_050 = results["bbd_character_050"]["rms_diff_overall"]
    diff_075 = results["bbd_character_075"]["rms_diff_overall"]
    diff_100 = results["bbd_character_100"]["rms_diff_overall"]

    monotonic = (0.0 < diff_025 < diff_050 < diff_075 < diff_100)
    print("\n--- 6. EFFECT INTENSITY & MONOTONICITY EVALUATION ---")
    print(f"Progressive RMS deviation strictly monotonic: {'YES (PASS)' if monotonic else 'NO (FAIL)'}")
    print(f"  25%:  {diff_025:.6f}")
    print(f"  50%:  {diff_050:.6f}")
    print(f"  75%:  {diff_075:.6f}")
    print(f"  100%: {diff_100:.6f}")
    print("\nNOTE ON MONOTONICITY & MUSICAL QUALITY:")
    print("  While RMS deviation from 0% scales monotonically with bbdCharacter, RMS deviation")
    print("  is an indicator of acoustic departure, NOT a linear measure of musical quality.")
    print("  A setting of 50% provides balanced analog warmth and spatial softening; 100% provides")
    print("  pronounced vintage BBD character. Neither is inherently 'better' in artistic context.")

    results["monotonicity_pass"] = monotonic
    results["monotonicity_note"] = "RMS deviation is monotonic with bbdCharacter. Not a subjective ranking."

    report_path = os.path.join(out_dir, "bbd_character_analysis_report.json")
    with open(report_path, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nFull JSON report written to: {report_path}")

    return 0 if monotonic else 1

if __name__ == "__main__":
    sys.exit(main())
