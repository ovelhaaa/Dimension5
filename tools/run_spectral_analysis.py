#!/usr/bin/env python3
"""
DIMENSION5 — BBD CHARACTER ENGINE 2.1
Comprehensive Spectral Validation, Sideband Resolution & Aliasing Runner
Phase A.2 Validation Suite

Analyzes rendered 16-second fixtures to:
1. Resolve 0.25 Hz LFO modulation sidebands with high frequency resolution (df = 0.0625 Hz).
2. Measure exact carrier amplitude, sideband power, out-of-band spurious, and noise floor.
3. Scan for potential aliasing / Nyquist foldover across 44.1, 48, and 96 kHz.
4. Verify bit-exact neutrality against frozen e153da5 baseline when character = 0%.
5. Generate CSV, JSON reports and publication-quality spectral PNG plots.
"""

import os
import sys
import json
import csv
import numpy as np
from analyze_spectral_validation import (
    read_wav,
    analyze_carrier_and_sidebands,
    compare_block_invariance,
    plot_spectral_validation
)

def run_spectral_suite(out_dir="out", docs_dir="docs/spectral"):
    print("=" * 72)
    print("DIMENSION5 — PHASE A.2 HIGH-RESOLUTION SPECTRAL VALIDATION SUITE")
    print("=" * 72)
    os.makedirs(docs_dir, exist_ok=True)
    
    report = {
        "metadata": {
            "suite": "Phase A.2 Spectral Validation",
            "resolution_nominal_hz": 0.0625,
            "duration_sec": 16.0,
            "window": "4-term Blackman-Harris (-92 dB sidelobes)"
        },
        "neutrality_e153da5": {},
        "spectral_fixtures": {},
        "aliasing_evaluations": {},
        "summary_table": []
    }
    
    # -------------------------------------------------------------------------
    # 1. Neutrality against e153da5 baseline at 0% character (16s renders)
    # -------------------------------------------------------------------------
    print("\n--- 1. VERIFYING NEUTRALITY AGAINST e153da5 BASELINE (16-second audio) ---")
    freqs = ["1k", "5k", "10k", "15k", "18k"]
    all_neutral = True
    for f_label in freqs:
        f_char0 = os.path.join(out_dir, f"spec_{f_label}_48k_char000_f32.wav")
        f_base = os.path.join(out_dir, f"spec_{f_label}_48k_e153da5_f32.wav")
        if os.path.exists(f_char0) and os.path.exists(f_base):
            l0, r0, sr0 = read_wav(f_char0)
            lb, rb, srb = read_wav(f_base)
            comp_l = compare_block_invariance(lb, l0, sr0, f"neutrality_{f_label}_left")
            comp_r = compare_block_invariance(rb, r0, sr0, f"neutrality_{f_label}_right")
            is_exact = comp_l["is_bit_exact"] and comp_r["is_bit_exact"]
            all_neutral &= is_exact
            report["neutrality_e153da5"][f_label] = {
                "is_bit_exact": is_exact,
                "max_abs_diff_l": comp_l["max_abs_diff"],
                "max_abs_diff_r": comp_r["max_abs_diff"]
            }
            print(f"  {f_label} 48kHz (16s): Bit-Exact = {is_exact} (MaxDiff L={comp_l['max_abs_diff']}, R={comp_r['max_abs_diff']})")

    # -------------------------------------------------------------------------
    # 2. Sideband Resolution and Spectral Analysis across Carriers at 48 kHz
    # -------------------------------------------------------------------------
    print("\n--- 2. CARRIER & SIDEBAND RESOLUTION AT 48 kHz (16s, df = 0.0625 Hz) ---")
    carrier_freqs = [
        ("1k", 1000.0),
        ("5k", 5000.0),
        ("10k", 10000.0),
        ("15k", 15000.0),
        ("18k", 18000.0)
    ]
    
    plot_dict_1k = {}
    plot_dict_chars_10k = {}

    for label, fc in carrier_freqs:
        for char_lvl, char_tag in [(0.0, "000"), (0.5, "050"), (1.0, "100")]:
            wav_path = os.path.join(out_dir, f"spec_{label}_48k_char{char_tag}_f32.wav")
            if not os.path.exists(wav_path):
                continue
            l, r, sr = read_wav(wav_path)
            res = analyze_carrier_and_sidebands(l, sr, f_carrier=fc, f_mod=0.25, max_delay_depth_ms=1.4)
            
            fixture_key = f"{label}_48k_char{char_tag}"
            
            # Store lean record in report (excluding giant raw freqs array for json)
            res_lean = {k: v for k, v in res.items() if k not in ["freqs", "mag_db"]}
            report["spectral_fixtures"][fixture_key] = res_lean
            
            # Add to summary table
            report["summary_table"].append({
                "fixture": fixture_key,
                "sr": sr,
                "carrier_nom_hz": fc,
                "carrier_meas_hz": res["measured_carrier_hz"],
                "carrier_dbfs": res["carrier_peak_dbfs"],
                "sideband_power_dbc": res["sideband_power_rel_dbc"],
                "sfdr_db": res["sfdr_db"],
                "max_spur_freq_hz": res["max_spurious_freq_hz"],
                "max_spur_dbc": res["max_spurious_dbc"],
                "noise_floor_dbfs": res["noise_floor_median_dbfs"]
            })
            
            print(f"[{fixture_key:16s}] Carrier: {res['carrier_peak_dbfs']:6.2f} dBFS | "
                  f"Sidebands: {res['sideband_power_rel_dbc']:6.2f} dBc | "
                  f"SFDR: {res['sfdr_db']:5.1f} dB | "
                  f"NoiseFloor: {res['noise_floor_median_dbfs']:6.1f} dBFS")

            if label == "1k":
                plot_dict_1k[f"Character {int(char_lvl*100)}%"] = res
            if label == "10k":
                plot_dict_chars_10k[f"Character {int(char_lvl*100)}%"] = res

    # -------------------------------------------------------------------------
    # 3. Multi-Rate Aliasing Scans (44.1, 48, 96 kHz)
    # -------------------------------------------------------------------------
    print("\n--- 3. MULTI-RATE ALIASING / NYQUIST SCAN (44.1k vs 48k vs 96k) ---")
    plot_dict_10k_rates = {}
    plot_dict_15k_rates = {}
    
    for f_label, fc in [("10k", 10000.0), ("15k", 15000.0)]:
        print(f"\nEvaluating Carrier = {fc:.0f} Hz across sample rates:")
        for rate, tag_sr in [(44100, "44k"), (48000, "48k"), (96000, "96k")]:
            wav_path = os.path.join(out_dir, f"spec_{f_label}_{tag_sr}_char100_f32.wav")
            if not os.path.exists(wav_path):
                continue
            l, r, sr = read_wav(wav_path)
            res = analyze_carrier_and_sidebands(l, sr, f_carrier=fc, f_mod=0.25, max_delay_depth_ms=1.4)
            key = f"{f_label}_{tag_sr}_char100"
            
            # Check aliasing candidates
            sig_aliases = [c for c in res["aliasing_candidates"] if c["is_significant"]]
            report["aliasing_evaluations"][key] = {
                "carrier_hz": fc,
                "sr": rate,
                "sfdr_db": res["sfdr_db"],
                "max_spurious_freq_hz": res["max_spurious_freq_hz"],
                "max_spurious_dbc": res["max_spurious_dbc"],
                "significant_aliasing_count": len(sig_aliases),
                "candidates": res["aliasing_candidates"]
            }
            
            status = "CLEAN (PASS)" if len(sig_aliases) == 0 else f"FLAGGED ({len(sig_aliases)} aliases)"
            print(f"  [{tag_sr:4s} | fc={fc:5.0f} Hz] SFDR: {res['sfdr_db']:5.1f} dB | "
                  f"Max Spur: {res['max_spurious_dbc']:6.1f} dBc @ {res['max_spurious_freq_hz']:8.1f} Hz | {status}")
            
            if f_label == "10k":
                plot_dict_10k_rates[f"{tag_sr} (fs={rate})"] = res
            if f_label == "15k":
                plot_dict_15k_rates[f"{tag_sr} (fs={rate})"] = res

    # -------------------------------------------------------------------------
    # 4. Generate Verification Plots
    # -------------------------------------------------------------------------
    print("\n--- 4. GENERATING PUBLICATION-QUALITY SPECTRAL PLOTS ---")
    if len(plot_dict_1k) > 0:
        p1 = os.path.join(docs_dir, "spectral_1k_sidebands_resolution.png")
        plot_spectral_validation(
            plot_dict_1k, p1,
            "Dimension5 BBD Character Engine 2.1 — 1 kHz Carrier Sideband Resolution (Δf = 0.0625 Hz)"
        )
        print(f"  Saved: {p1}")

    if len(plot_dict_chars_10k) > 0:
        p2 = os.path.join(docs_dir, "spectral_10k_character_scaling.png")
        plot_spectral_validation(
            plot_dict_chars_10k, p2,
            "Dimension5 BBD Character Engine 2.1 — 10 kHz Dispersion Softening & Character Progression"
        )
        print(f"  Saved: {p2}")

    if len(plot_dict_10k_rates) > 0:
        p3 = os.path.join(docs_dir, "spectral_10k_multirate_aliasing.png")
        plot_spectral_validation(
            plot_dict_10k_rates, p3,
            "Dimension5 BBD Character Engine 2.1 — 10 kHz Multi-Rate Nyquist Aliasing Verification"
        )
        print(f"  Saved: {p3}")

    if len(plot_dict_15k_rates) > 0:
        p4 = os.path.join(docs_dir, "spectral_15k_multirate_aliasing.png")
        plot_spectral_validation(
            plot_dict_15k_rates, p4,
            "Dimension5 BBD Character Engine 2.1 — 15 kHz Multi-Rate Nyquist Aliasing Verification"
        )
        print(f"  Saved: {p4}")

    # -------------------------------------------------------------------------
    # 5. Export JSON and CSV Reports
    # -------------------------------------------------------------------------
    json_path = os.path.join(out_dir, "spectral_validation_report.json")
    with open(json_path, 'w') as f:
        json.dump(report, f, indent=2)
    print(f"\nJSON report written to: {json_path}")
    
    csv_path = os.path.join(out_dir, "spectral_validation_summary.csv")
    if len(report["summary_table"]) > 0:
        keys = list(report["summary_table"][0].keys())
        with open(csv_path, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=keys)
            writer.writeheader()
            writer.writerows(report["summary_table"])
        print(f"CSV summary written to: {csv_path}")

    print("\n" + "=" * 72)
    print(f"SPECTRAL SUITE COMPLETE: Neutrality Pass = {all_neutral}")
    print("=" * 72)
    return 0 if all_neutral else 1

if __name__ == '__main__':
    out_directory = sys.argv[1] if len(sys.argv) > 1 else "out"
    sys.exit(run_spectral_suite(out_directory))
