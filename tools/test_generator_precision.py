#!/usr/bin/env python3
"""
DIMENSION5 — BBD CHARACTER ENGINE 2.1
Spectral Generator Precision & Numerical Stability Validator
Phase A.3 Validation Suite

Demonstrates numerically the elimination of float32 phase-quantization
jitter artifacts over 16-second durations across 44.1, 48, and 96 kHz:
- Compares Old Generator (float32 time + sinf) vs Fixed Generator (double time/phase + sin)
- Measures carrier accuracy, frequency error, phase jitter, SFDR, out-of-band energy,
  and residual error against pure double-precision ground truth.
- Exports generator_precision_report.json and generator_precision_comparison.png.
"""

import os
import sys
import json
import math
import numpy as np
import scipy.signal as sig
import scipy.fft as fft
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def generate_signals(f_hz, sr, duration_sec=16.0):
    n = int(round(sr * duration_sec))
    
    # 1. Pure Double-Precision Mathematical Ground Truth (float64 throughout)
    t_double = np.arange(n, dtype=np.float64) / float(sr)
    phase_truth = 2.0 * np.pi * float(f_hz) * t_double
    sig_truth_double = 0.25 * np.sin(phase_truth)
    
    # 2. Old Generator: single-precision float32 time, float32 angle multiplication, sinf
    t_f32 = (np.arange(n, dtype=np.float32) / np.float32(sr)).astype(np.float32)
    two_pi_f = np.float32(2.0 * 3.14159265358979323846)
    freq_f32 = np.float32(f_hz)
    arg_f32 = (two_pi_f * freq_f32 * t_f32).astype(np.float32)
    sig_old = (np.float32(0.25) * np.sin(arg_f32)).astype(np.float32)
    
    # 3. Fixed Generator: double-precision time and phase, fmod reduction, converted to float32
    two_pi_d = 2.0 * np.pi
    phase_d = np.fmod(two_pi_d * float(f_hz) * t_double, two_pi_d)
    sig_new = (0.25 * np.sin(phase_d)).astype(np.float32)
    
    return n, t_double, sig_truth_double, sig_old, sig_new

def analyze_spectral_purity(signal, sr, f_carrier):
    n = len(signal)
    win = sig.windows.blackmanharris(n)
    cg = np.sum(win)
    spec = fft.rfft(signal * win)
    freqs = fft.rfftfreq(n, d=1.0 / sr)
    
    lin_mag = (2.0 / cg) * np.abs(spec)
    lin_mag[0] *= 0.5
    if n % 2 == 0:
        lin_mag[-1] *= 0.5
        
    eps = 1e-15
    mag_dbfs = 20.0 * np.log10(np.maximum(lin_mag, eps))
    df = float(sr) / float(n)
    
    # Carrier peak
    c_mask = np.abs(freqs - f_carrier) <= max(2.0, 4.0 * df)
    c_bins = np.where(c_mask)[0]
    carrier_bin = c_bins[np.argmax(mag_dbfs[c_bins])] if len(c_bins) > 0 else np.argmin(np.abs(freqs - f_carrier))
    
    carrier_meas_f = freqs[carrier_bin]
    carrier_peak_dbfs = mag_dbfs[carrier_bin]
    freq_err_hz = abs(carrier_meas_f - f_carrier)
    
    # Out of band mask (> 5 Hz away from carrier)
    oob_mask = np.abs(freqs - f_carrier) > 5.0
    # Also exclude DC (0-10 Hz)
    oob_mask &= (freqs > 10.0)
    
    oob_bins = np.where(oob_mask)[0]
    if len(oob_bins) > 0:
        max_spur_bin = oob_bins[np.argmax(lin_mag[oob_bins])]
        max_spur_freq = float(freqs[max_spur_bin])
        max_spur_dbfs = float(mag_dbfs[max_spur_bin])
        sfdr_db = float(carrier_peak_dbfs - max_spur_dbfs)
        noise_floor_median_dbfs = float(np.median(mag_dbfs[oob_bins]))
        noise_floor_rms_dbfs = float(10.0 * np.log10(np.maximum(np.mean(lin_mag[oob_bins]**2), 1e-15)))
    else:
        max_spur_freq = 0.0
        max_spur_dbfs = -160.0
        sfdr_db = 160.0
        noise_floor_median_dbfs = -160.0
        noise_floor_rms_dbfs = -160.0
        
    return {
        "carrier_meas_hz": float(carrier_meas_f),
        "carrier_peak_dbfs": float(carrier_peak_dbfs),
        "freq_error_hz": float(freq_err_hz),
        "sfdr_db": float(sfdr_db),
        "max_spur_freq_hz": float(max_spur_freq),
        "max_spur_dbfs": float(max_spur_dbfs),
        "noise_floor_median_dbfs": float(noise_floor_median_dbfs),
        "noise_floor_rms_dbfs": float(noise_floor_rms_dbfs),
        "freqs": freqs,
        "mag_dbfs": mag_dbfs
    }

def run_generator_precision_suite(out_dir="out", docs_dir="docs/spectral"):
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(docs_dir, exist_ok=True)
    
    print("=" * 76)
    print("DIMENSION5 — INDEPENDENT SPECTRAL GENERATOR PRECISION SUITE")
    print("=" * 76)
    
    test_freqs = [1000.0, 5000.0, 10000.0, 15000.0, 18000.0]
    sample_rates = [44100, 48000, 96000]
    duration = 16.0
    
    results = {
        "metadata": {
            "test_duration_sec": duration,
            "tested_frequencies_hz": test_freqs,
            "tested_sample_rates_hz": sample_rates,
            "window": "4-term Blackman-Harris",
            "float32_eps": float(np.finfo(np.float32).eps)
        },
        "comparisons": [],
        "overall_summary": {}
    }
    
    all_passed = True
    plot_data_18k_96k = {}
    
    print(f"\n{'SR (Hz)':<8} | {'Freq (Hz)':<10} | {'Metric':<16} | {'Old (float32)':<16} | {'New (double)':<16} | {'Status'}")
    print("-" * 80)
    
    for sr in sample_rates:
        for f_hz in test_freqs:
            n, t, ref_truth, sig_old, sig_new = generate_signals(f_hz, sr, duration)
            
            # Ground truth comparison
            err_old = np.abs(sig_old - ref_truth)
            err_new = np.abs(sig_new - ref_truth)
            
            max_err_old = float(np.max(err_old))
            rms_err_old = float(np.sqrt(np.mean(err_old ** 2)))
            max_err_new = float(np.max(err_new))
            rms_err_new = float(np.sqrt(np.mean(err_new ** 2)))
            
            spec_old = analyze_spectral_purity(sig_old, sr, f_hz)
            spec_new = analyze_spectral_purity(sig_new, sr, f_hz)
            
            # The fixed generator MUST have SFDR >= 115 dB (typically ~135-145 dB)
            # and max time-domain error <= float32 precision (~1.5e-7 for 0.25 amplitude)
            pass_sfdr = spec_new["sfdr_db"] >= 115.0
            pass_err = max_err_new < 1.0e-6
            passed = pass_sfdr and pass_err
            all_passed &= passed
            status_str = "PASS" if passed else "FAIL"
            
            record = {
                "sample_rate": sr,
                "target_freq_hz": f_hz,
                "old_generator": {
                    "max_err_vs_truth": max_err_old,
                    "rms_err_vs_truth": rms_err_old,
                    "sfdr_db": spec_old["sfdr_db"],
                    "max_spur_freq_hz": spec_old["max_spur_freq_hz"],
                    "max_spur_dbfs": spec_old["max_spur_dbfs"],
                    "noise_floor_median_dbfs": spec_old["noise_floor_median_dbfs"],
                    "noise_floor_rms_dbfs": spec_old["noise_floor_rms_dbfs"]
                },
                "fixed_generator": {
                    "max_err_vs_truth": max_err_new,
                    "rms_err_vs_truth": rms_err_new,
                    "sfdr_db": spec_new["sfdr_db"],
                    "max_spur_freq_hz": spec_new["max_spur_freq_hz"],
                    "max_spur_dbfs": spec_new["max_spur_dbfs"],
                    "noise_floor_median_dbfs": spec_new["noise_floor_median_dbfs"],
                    "noise_floor_rms_dbfs": spec_new["noise_floor_rms_dbfs"]
                },
                "improvement": {
                    "sfdr_gain_db": spec_new["sfdr_db"] - spec_old["sfdr_db"],
                    "error_reduction_factor": max_err_old / max(1e-12, max_err_new)
                },
                "status": status_str
            }
            results["comparisons"].append(record)
            
            print(f"{sr:<8} | {f_hz:<10.0f} | SFDR (dB)        | {spec_old['sfdr_db']:<16.1f} | {spec_new['sfdr_db']:<16.1f} | {status_str}")
            print(f"{'':<8} | {'':<10} | Max Abs Error    | {max_err_old:<16.2e} | {max_err_new:<16.2e} |")
            print(f"{'':<8} | {'':<10} | Floor (dBFS)     | {spec_old['noise_floor_median_dbfs']:<16.1f} | {spec_new['noise_floor_median_dbfs']:<16.1f} |")
            print("-" * 80)
            
            # Save worst-case condition (18 kHz @ 96 kHz) for plotting
            if sr == 96000 and f_hz == 18000.0:
                plot_data_18k_96k = {
                    "f_hz": f_hz,
                    "sr": sr,
                    "spec_old": spec_old,
                    "spec_new": spec_new,
                    "t": t[-500:],
                    "sig_old_tail": sig_old[-500:],
                    "sig_new_tail": sig_new[-500:],
                    "ref_tail": ref_truth[-500:]
                }
                
    # -------------------------------------------------------------------------
    # Visualization: Compare Old vs Fixed Generator
    # -------------------------------------------------------------------------
    if plot_data_18k_96k:
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))
        fig.suptitle("Dimension5 — Generator Precision Comparison: 18 kHz at 96 kHz (16-second render)", fontsize=13, fontweight='bold')
        
        f_old = plot_data_18k_96k["spec_old"]["freqs"]
        m_old = plot_data_18k_96k["spec_old"]["mag_dbfs"]
        f_new = plot_data_18k_96k["spec_new"]["freqs"]
        m_new = plot_data_18k_96k["spec_new"]["mag_dbfs"]
        
        ax1.plot(f_old, m_old, label=f"Old Generator (float32 time/sinf, SFDR={plot_data_18k_96k['spec_old']['sfdr_db']:.1f} dB)", color='#d62728', alpha=0.8, linewidth=1.0)
        ax1.plot(f_new, m_new, label=f"Fixed Generator (double phase/sin, SFDR={plot_data_18k_96k['spec_new']['sfdr_db']:.1f} dB)", color='#2ca02c', alpha=0.85, linewidth=1.0)
        ax1.set_title("Full Spectrum across Nyquist: Elimination of Phase-Jitter Spurious Noise")
        ax1.set_xlabel("Frequency (Hz)")
        ax1.set_ylabel("Magnitude (dBFS)")
        ax1.set_ylim(-160, 0)
        ax1.grid(True, linestyle=':', alpha=0.6)
        ax1.legend(loc='upper right')
        
        # Zoom around carrier
        fc = 18000.0
        mask_old = (f_old >= fc - 200.0) & (f_old <= fc + 200.0)
        mask_new = (f_new >= fc - 200.0) & (f_new <= fc + 200.0)
        ax2.plot(f_old[mask_old] - fc, m_old[mask_old], label="Old Generator (spurious skirts)", color='#d62728', alpha=0.8, linewidth=1.2)
        ax2.plot(f_new[mask_new] - fc, m_new[mask_new], label="Fixed Generator (clean delta carrier)", color='#2ca02c', alpha=0.9, linewidth=1.2)
        ax2.set_title("Close Carrier Zoom (±200 Hz): Clean Fundamental Resolution")
        ax2.set_xlabel("Offset from Carrier (Hz)")
        ax2.set_ylabel("Magnitude (dBFS)")
        ax2.set_ylim(-160, 0)
        ax2.grid(True, linestyle=':', alpha=0.6)
        ax2.legend(loc='upper right')
        
        plt.tight_layout()
        plot_path = os.path.join(docs_dir, "generator_precision_comparison.png")
        plt.savefig(plot_path, dpi=180)
        plt.close()
        print(f"\nSaved precision plot to: {plot_path}")
        
    results["overall_summary"] = {
        "all_tests_passed": all_passed,
        "total_conditions": len(results["comparisons"]),
        "min_new_sfdr_db": min(c["fixed_generator"]["sfdr_db"] for c in results["comparisons"]),
        "max_new_sfdr_db": max(c["fixed_generator"]["sfdr_db"] for c in results["comparisons"]),
        "max_new_error_vs_truth": max(c["fixed_generator"]["max_err_vs_truth"] for c in results["comparisons"])
    }
    
    report_path = os.path.join(out_dir, "generator_precision_report.json")
    with open(report_path, 'w') as f:
        json.dump(results, f, indent=2)
    print(f"Saved generator precision report to: {report_path}")
    print(f"\nGENERATOR PRECISION SUITE FINISHED: All Passed = {all_passed}")
    print("=" * 76)
    
    return 0 if all_passed else 1

if __name__ == '__main__':
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "out"
    sys.exit(run_generator_precision_suite(out_dir))
