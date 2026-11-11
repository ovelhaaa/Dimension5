#!/usr/bin/env python3
"""
DIMENSION5 — BBD CHARACTER ENGINE 2.1
Audio Characterization & Neutrality Analysis Harness
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
    print("=" * 64)
    print("DIMENSION5 — BBD CHARACTER ENGINE 2.1 ANALYSIS")
    print("=" * 64)

    # 1. Neutrality comparison
    ref_path = os.path.join(out_dir, "bbd_character_000.wav")
    if not os.path.exists(ref_path):
        print(f"File not found: {ref_path}")
        return 1

    ref_l, ref_r, sr = read_wav(ref_path)
    ref_peak_l, ref_dc_l, ref_rms_l = compute_metrics(ref_l)
    ref_peak_r, ref_dc_r, ref_rms_r = compute_metrics(ref_r)

    print(f"\n[REFERENCE] bbd_character_000.wav (48 kHz, {len(ref_l)} frames)")
    print(f"  Left  : Peak={ref_peak_l:.4f}, DC={ref_dc_l:.6f}, RMS={ref_rms_l:.4f}")
    print(f"  Right : Peak={ref_peak_r:.4f}, DC={ref_dc_r:.6f}, RMS={ref_rms_r:.4f}")

    levels = ["025", "050", "075", "100"]
    results = {}

    print("\n--- PROGRESSIVE BBD CHARACTER COMPARISON (Full Mix) ---")
    for lvl in levels:
        test_path = os.path.join(out_dir, f"bbd_character_{lvl}.wav")
        test_l, test_r, _ = read_wav(test_path)
        comp = compare_signals(ref_l, ref_r, test_l, test_r)
        results[f"bbd_character_{lvl}"] = comp
        print(f"BBD Character {int(lvl)}% vs 0%:")
        print(f"  Max Diff: {comp['max_diff_overall']:.6f} (L: {comp['max_diff_left']:.6f}, R: {comp['max_diff_right']:.6f})")
        print(f"  RMS Diff: {comp['rms_diff_overall']:.6f} | Divergent: {comp['divergent_samples']}/{comp['total_samples']}")

    print("\n--- WET-ONLY PROGRESSION ---")
    wet_ref_path = os.path.join(out_dir, "bbd_character_000_wet.wav")
    wet_ref_l, wet_ref_r, _ = read_wav(wet_ref_path)
    for lvl in ["050", "100"]:
        wet_test_path = os.path.join(out_dir, f"bbd_character_{lvl}_wet.wav")
        wet_l, wet_r, _ = read_wav(wet_test_path)
        comp = compare_signals(wet_ref_l, wet_ref_r, wet_l, wet_r)
        results[f"bbd_character_{lvl}_wet"] = comp
        print(f"Wet-Only BBD Character {int(lvl)}% vs 0%:")
        print(f"  Max Diff: {comp['max_diff_overall']:.6f} | RMS Diff: {comp['rms_diff_overall']:.6f}")

    print("\n--- MULTITONE & IMPULSES ---")
    for name in ["bbd_multitone", "bbd_impulses"]:
        p0 = os.path.join(out_dir, f"{name}_000.wav")
        p1 = os.path.join(out_dir, f"{name}_100.wav")
        l0, r0, _ = read_wav(p0)
        l1, r1, _ = read_wav(p1)
        comp = compare_signals(l0, r0, l1, r1)
        results[f"{name}_100_vs_000"] = comp
        print(f"{name} 100% vs 0%:")
        print(f"  Max Diff: {comp['max_diff_overall']:.6f} | RMS Diff: {comp['rms_diff_overall']:.6f}")

    report_path = os.path.join(out_dir, "bbd_character_analysis_report.json")
    with open(report_path, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nFull report written to: {report_path}")

    # Monotonicity check
    diff_025 = results["bbd_character_025"]["rms_diff_overall"]
    diff_050 = results["bbd_character_050"]["rms_diff_overall"]
    diff_075 = results["bbd_character_075"]["rms_diff_overall"]
    diff_100 = results["bbd_character_100"]["rms_diff_overall"]

    monotonic = (0.0 < diff_025 < diff_050 < diff_075 < diff_100)
    print(f"\nProgressive scaling strictly monotonic: {'YES (PASS)' if monotonic else 'NO (FAIL)'}")
    print(f"  25%:  {diff_025:.6f}")
    print(f"  50%:  {diff_050:.6f}")
    print(f"  75%:  {diff_075:.6f}")
    print(f"  100%: {diff_100:.6f}")

    return 0 if monotonic else 1

if __name__ == "__main__":
    sys.exit(main())
