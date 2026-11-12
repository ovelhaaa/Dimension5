#!/usr/bin/env python3
"""
tools/compare_phase_a4_b1.py

Comparative Audio & Spectral Characterization:
Phase A.4 (Static Pre/Post BBD Filters) vs Phase B.1 (Clock-Tracking Spectral Character)

Evaluates:
- Peak & RMS level preservation
- Max absolute difference & RMS difference
- Stereo correlation & Side-to-Mid energy ratio
- High-frequency roll-off & spectral redistribution
- Cross-correlation between Phase A.4 and Phase B.1

Exports out/phase_a4_vs_phase_b1_report.json
"""

import sys
import os
import json
import math
import numpy as np
from scipy.io import wavfile

def compute_stereo_metrics(data):
    # data is float32 (samples, channels)
    l = data[:, 0]
    r = data[:, 1]
    n = len(l)
    
    mean_l = float(np.mean(l))
    mean_r = float(np.mean(r))
    var_l = float(np.mean((l - mean_l)**2))
    var_r = float(np.mean((r - mean_r)**2))
    cov = float(np.mean((l - mean_l) * (r - mean_r)))
    denom = math.sqrt(max(1e-12, var_l * var_r))
    corr = cov / denom
    
    mid = 0.5 * (l + r)
    side = 0.5 * (l - r)
    mid_energy = float(np.mean(mid**2))
    side_energy = float(np.mean(side**2))
    side_mid_ratio = side_energy / max(1e-12, mid_energy)
    
    return {
        "stereo_correlation": round(corr, 4),
        "mid_energy": round(mid_energy, 6),
        "side_energy": round(side_energy, 6),
        "side_mid_ratio": round(side_mid_ratio, 4)
    }

def analyze_ab_pair(path_a4, path_b1, label):
    sr_a4, data_a4 = wavfile.read(path_a4)
    sr_b1, data_b1 = wavfile.read(path_b1)
    
    assert sr_a4 == sr_b1, f"Sample rate mismatch: {sr_a4} vs {sr_b1}"
    assert len(data_a4) == len(data_b1), f"Length mismatch: {len(data_a4)} vs {len(data_b1)}"
    
    if data_a4.dtype == np.int16:
        a4 = data_a4.astype(np.float32) / 32768.0
        b1 = data_b1.astype(np.float32) / 32768.0
    else:
        a4 = data_a4.astype(np.float32)
        b1 = data_b1.astype(np.float32)
        
    diff = b1 - a4
    max_diff = float(np.max(np.abs(diff)))
    rms_diff = float(np.sqrt(np.mean(diff**2)))
    
    peak_a4 = float(np.max(np.abs(a4)))
    peak_b1 = float(np.max(np.abs(b1)))
    rms_a4 = float(np.sqrt(np.mean(a4**2)))
    rms_b1 = float(np.sqrt(np.mean(b1**2)))
    
    gain_diff_db = 20.0 * math.log10(max(1e-9, rms_b1) / max(1e-9, rms_a4))
    
    # Cross-version correlation (L and R)
    corr_l = float(np.corrcoef(a4[:, 0], b1[:, 0])[0, 1])
    corr_r = float(np.corrcoef(a4[:, 1], b1[:, 1])[0, 1])
    cross_corr = 0.5 * (corr_l + corr_r)
    
    st_a4 = compute_stereo_metrics(a4)
    st_b1 = compute_stereo_metrics(b1)
    
    # Spectral comparison (FFT magnitude ratio in HF band: 6k to 15k)
    w = np.hanning(len(a4))
    fft_a4 = np.abs(np.fft.rfft(a4[:, 0] * w))
    fft_b1 = np.abs(np.fft.rfft(b1[:, 0] * w))
    freqs = np.fft.rfftfreq(len(a4), 1.0 / sr_a4)
    
    hf_mask = (freqs >= 6000.0) & (freqs <= 15000.0)
    hf_energy_a4 = float(np.sum(fft_a4[hf_mask]**2))
    hf_energy_b1 = float(np.sum(fft_b1[hf_mask]**2))
    hf_roll_off_db = 10.0 * math.log10(max(1e-12, hf_energy_b1) / max(1e-12, hf_energy_a4))
    
    return {
        "fixture_label": label,
        "sample_rate": sr_a4,
        "phase_a4": {
            "peak": round(peak_a4, 4),
            "rms": round(rms_a4, 4),
            "stereo_metrics": st_a4
        },
        "phase_b1": {
            "peak": round(peak_b1, 4),
            "rms": round(rms_b1, 4),
            "stereo_metrics": st_b1
        },
        "comparison": {
            "max_abs_diff": round(max_diff, 6),
            "rms_diff": round(rms_diff, 6),
            "rms_diff_dbfs": round(20.0 * math.log10(max(1e-12, rms_diff)), 2),
            "gain_diff_db": round(gain_diff_db, 3),
            "cross_version_correlation": round(cross_corr, 5),
            "hf_6k_15k_roll_off_db": round(hf_roll_off_db, 2)
        }
    }

def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "out"
    print("=" * 76)
    print("DIMENSION5 — PHASE A.4 vs PHASE B.1 AUDITION & SPECTRAL COMPARISON")
    print("=" * 76)
    
    pairs = [
        ("ab_phase_a4_char050_synth.wav",  "ab_phase_b1_char050_synth.wav",  "Synth Pluck (50% Character)"),
        ("ab_phase_a4_char100_synth.wav",  "ab_phase_b1_char100_synth.wav",  "Synth Pluck (100% Character)"),
        ("ab_phase_a4_char050_guitar.wav", "ab_phase_b1_char050_guitar.wav", "Clean Guitar (50% Character)"),
        ("ab_phase_a4_char100_guitar.wav", "ab_phase_b1_char100_guitar.wav", "Clean Guitar (100% Character)"),
        ("ab_phase_a4_char050_pad.wav",    "ab_phase_b1_char050_pad.wav",    "Harmonic Pad (50% Character)"),
        ("ab_phase_a4_char100_pad.wav",    "ab_phase_b1_char100_pad.wav",    "Harmonic Pad (100% Character)"),
        ("ab_phase_a4_char050_stereo.wav", "ab_phase_b1_char050_stereo.wav", "Stereo Asymmetric (50% Character)"),
        ("ab_phase_a4_char100_stereo.wav", "ab_phase_b1_char100_stereo.wav", "Stereo Asymmetric (100% Character)"),
        ("ab_phase_a4_char050_wet.wav",    "ab_phase_b1_char050_wet.wav",    "Wet-Only Pad (50% Character)"),
        ("ab_phase_a4_char100_wet.wav",    "ab_phase_b1_char100_wet.wav",    "Wet-Only Pad (100% Character)")
    ]
    
    results = []
    for fn_a4, fn_b1, label in pairs:
        p_a4 = os.path.join(out_dir, fn_a4)
        p_b1 = os.path.join(out_dir, fn_b1)
        if not os.path.exists(p_a4) or not os.path.exists(p_b1):
            print(f"Skipping {label}: file not found ({fn_a4} or {fn_b1})")
            continue
            
        res = analyze_ab_pair(p_a4, p_b1, label)
        results.append(res)
        c = res["comparison"]
        print(f"[{res['fixture_label']}]")
        print(f"  Max Diff: {c['max_abs_diff']:.6f} | RMS Diff: {c['rms_diff']:.6f} ({c['rms_diff_dbfs']} dBFS)")
        print(f"  Level Change: {c['gain_diff_db']:+.2f} dB | Cross Correlation: {c['cross_version_correlation']:.4f}")
        print(f"  Stereo Side/Mid: A4={res['phase_a4']['stereo_metrics']['side_mid_ratio']:.4f} -> B1={res['phase_b1']['stereo_metrics']['side_mid_ratio']:.4f}")
        print(f"  HF (6k-15k) Spectral Change: {c['hf_6k_15k_roll_off_db']:+.2f} dB\n")
        
    out_json = os.path.join(out_dir, "phase_a4_vs_phase_b1_report.json")
    with open(out_json, "w") as f:
        json.dump({
            "metadata": {
                "title": "Dimension5 Phase A.4 vs Phase B.1 Comparison",
                "baseline_commit": "37591d09ee32647910678400b95075d6d7118a87",
                "phase_b1_branch": "feature/bbd-spectral-character-phase-b1"
            },
            "evaluations": results
        }, f, indent=2)
    print(f"Report saved to: {out_json}")

if __name__ == "__main__":
    main()
