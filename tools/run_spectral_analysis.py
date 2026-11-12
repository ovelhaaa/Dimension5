#!/usr/bin/env python3
"""
DIMENSION5 — BBD CHARACTER ENGINE 2.1
Comprehensive Spectral Validation, Sideband Resolution & Aliasing Runner
Phase A.3 Validation Suite

Audits all rendered 16-second fixtures to:
1. Validate presence, WAV headers, float32 format, stereo channels, sample rates,
   duration (>= 16s), and absence of non-finite (NaN/Inf) samples.
2. Verify bit-exact neutrality against frozen e153da5 baseline when character = 0%.
3. Resolve 0.25 Hz LFO modulation sidebands with high resolution (df = 0.0625 Hz).
4. Measure exact carrier amplitude, sideband power, out-of-band spurious, and noise floor.
5. Scan for potential aliasing / Nyquist foldover across 44.1, 48, and 96 kHz.
6. Enforce explicit PASS/FAIL/NOT_RUN status on all required tests.
   Returns exit code 0 ONLY if all tests pass; exits non-zero on any FAIL or NOT_RUN.
7. Generate CSV, JSON reports and publication-quality spectral PNG plots.
"""

import os
import sys
import json
import csv
import datetime
import subprocess
import numpy as np
import scipy
import matplotlib
from analyze_spectral_validation import (
    validate_wav_file,
    analyze_carrier_and_sidebands,
    compare_block_invariance,
    plot_spectral_validation,
    classify_aliasing_components
)

def get_git_commit():
    try:
        res = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, check=True)
        return res.stdout.strip()
    except Exception:
        return "unknown"

def run_spectral_suite(out_dir="out", docs_dir="docs/spectral"):
    print("=" * 76)
    print("DIMENSION5 — PHASE A.3 HIGH-RESOLUTION SPECTRAL VALIDATION SUITE")
    print("=" * 76)
    os.makedirs(docs_dir, exist_ok=True)
    
    start_time = datetime.datetime.now(datetime.timezone.utc).isoformat()
    git_commit = get_git_commit()
    
    test_results = {}
    failures = []
    
    def record_test(test_id, category, status, metric_name, threshold, measured, reason=None):
        test_results[test_id] = {
            "test_id": test_id,
            "category": category,
            "status": status,
            "metric": metric_name,
            "threshold": threshold,
            "measured": measured,
            "reason": reason
        }
        if status != "PASS":
            failures.append(f"[{status}] {test_id}: {reason or status} (measured={measured}, threshold={threshold})")

    report = {
        "metadata": {
            "suite": "Phase A.3 Spectral Validation & Reliability",
            "git_commit": git_commit,
            "execution_date_utc": start_time,
            "tools": {
                "python": sys.version.split()[0],
                "numpy": np.__version__,
                "scipy": scipy.__version__,
                "matplotlib": matplotlib.__version__
            },
            "resolution_nominal_hz": 0.0625,
            "duration_sec": 16.0,
            "window": "4-term Blackman-Harris (-92 dB sidelobes)"
        },
        "test_results": test_results,
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
        test_id = f"neutrality_e153da5_{f_label}_48k"
        
        ok0, err0, l0, r0, sr0 = validate_wav_file(f_char0, expected_sr=48000, min_samples=48000 * 16)
        okb, errb, lb, rb, srb = validate_wav_file(f_base, expected_sr=48000, min_samples=48000 * 16)
        
        if not ok0:
            record_test(test_id, "neutrality", "FAIL", "bit_exact", "True", f"ERR_{err0}", f"Invalid char000 fixture: {err0}")
            all_neutral = False
            continue
        if not okb:
            record_test(test_id, "neutrality", "FAIL", "bit_exact", "True", f"ERR_{errb}", f"Invalid e153da5 fixture: {errb}")
            all_neutral = False
            continue
            
        comp_l = compare_block_invariance(lb, l0, sr0, f"neutrality_{f_label}_left")
        comp_r = compare_block_invariance(rb, r0, sr0, f"neutrality_{f_label}_right")
        is_exact = comp_l["is_bit_exact"] and comp_r["is_bit_exact"]
        max_diff = max(comp_l["max_abs_diff"], comp_r["max_abs_diff"])
        
        if is_exact:
            record_test(test_id, "neutrality", "PASS", "max_abs_diff", "== 0.0", f"{max_diff:.1e}")
        else:
            record_test(test_id, "neutrality", "FAIL", "max_abs_diff", "== 0.0", f"{max_diff:.1e}", "Divergent from e153da5 baseline")
            all_neutral = False
            
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
            test_id = f"spectral_{label}_48k_char{char_tag}"
            
            ok, err_code, l, r, sr = validate_wav_file(wav_path, expected_sr=48000, min_samples=48000 * 16)
            if not ok:
                record_test(test_id, "spectral_resolution", "FAIL", "fixture_integrity", "VALID", err_code, f"Fixture invalid: {err_code}")
                continue
                
            res = analyze_carrier_and_sidebands(l, sr, f_carrier=fc, f_mod=0.25, max_delay_depth_ms=1.4)
            
            # Grounded acceptance criteria:
            # - Carrier peak within normal envelope: [-16.0, -10.0] dBFS
            # - SFDR >= 45.0 dB
            # - Noise floor < -120.0 dBFS
            pass_carrier = -16.0 <= res["carrier_peak_dbfs"] <= -10.0
            pass_sfdr = res["sfdr_db"] >= 45.0
            pass_floor = res["noise_floor_median_dbfs"] < -120.0
            
            if pass_carrier and pass_sfdr and pass_floor:
                record_test(test_id, "spectral_resolution", "PASS", "sfdr_db", ">= 45.0 dB", f"{res['sfdr_db']:.1f} dB")
            else:
                reasons = []
                if not pass_carrier: reasons.append(f"carrier={res['carrier_peak_dbfs']:.1f}dBFS")
                if not pass_sfdr: reasons.append(f"sfdr={res['sfdr_db']:.1f}dB")
                if not pass_floor: reasons.append(f"floor={res['noise_floor_median_dbfs']:.1f}dBFS")
                record_test(test_id, "spectral_resolution", "FAIL", "sfdr_db", ">= 45.0 dB", f"{res['sfdr_db']:.1f} dB", ", ".join(reasons))
                
            fixture_key = f"{label}_48k_char{char_tag}"
            res_lean = {k: v for k, v in res.items() if k not in ["freqs", "mag_db"]}
            report["spectral_fixtures"][fixture_key] = res_lean
            
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
            test_id = f"multirate_aliasing_{f_label}_{tag_sr}_char100"
            
            ok, err_code, l, r, sr = validate_wav_file(wav_path, expected_sr=rate, min_samples=rate * 16)
            if not ok:
                record_test(test_id, "aliasing_multirate", "FAIL", "fixture_integrity", "VALID", err_code, f"Fixture invalid: {err_code}")
                continue
                
            res = analyze_carrier_and_sidebands(l, sr, f_carrier=fc, f_mod=0.25, max_delay_depth_ms=1.4)
            key = f"{f_label}_{tag_sr}_char100"
            
            # Baseline reference at same rate to separate inherent baseline foldover from BBD engine foldover
            wav_base = os.path.join(out_dir, f"spec_{f_label}_{tag_sr}_char000_f32.wav")
            base_candidates = []
            if os.path.exists(wav_base):
                ok_b, _, l_b, _, _ = validate_wav_file(wav_base, expected_sr=rate, min_samples=rate * 16)
                if ok_b:
                    res_b = analyze_carrier_and_sidebands(l_b, rate, f_carrier=fc, f_mod=0.25, max_delay_depth_ms=1.4)
                    base_candidates = res_b.get("aliasing_candidates", [])
                    
            # Component-by-component spectral matching against baseline
            df = res.get("freq_resolution_hz", 0.0625)
            classifications, new_aliases_count, amp_increases_count = classify_aliasing_components(
                res["aliasing_candidates"], base_candidates, df=df, amp_tol_db=6.0
            )
            
            sig_aliases = [c for c in res["aliasing_candidates"] if c["is_significant"]]
            pass_aliasing = (new_aliases_count == 0) and (amp_increases_count == 0) and (res["sfdr_db"] >= 45.0)
            
            if pass_aliasing:
                record_test(test_id, "aliasing_multirate", "PASS", "new_aliases", "0", f"{new_aliases_count} (SFDR={res['sfdr_db']:.1f} dB)")
            else:
                reasons = []
                if new_aliases_count > 0: reasons.append(f"{new_aliases_count} new alias component(s)")
                if amp_increases_count > 0: reasons.append(f"{amp_increases_count} amplitude increase(s)")
                if res["sfdr_db"] < 45.0: reasons.append(f"SFDR={res['sfdr_db']:.1f} dB (<45 dB)")
                record_test(test_id, "aliasing_multirate", "FAIL", "new_aliases", "0", f"{new_aliases_count}", ", ".join(reasons))
                
            report["aliasing_evaluations"][key] = {
                "carrier_hz": fc,
                "sr": rate,
                "sfdr_db": res["sfdr_db"],
                "max_spurious_freq_hz": res["max_spurious_freq_hz"],
                "max_spurious_dbc": res["max_spurious_dbc"],
                "significant_aliasing_count": len(sig_aliases),
                "new_aliasing_count": new_aliases_count,
                "amplitude_increases_count": amp_increases_count,
                "classifications": classifications,
                "candidates": res["aliasing_candidates"]
            }
            
            status = "CLEAN (PASS)" if pass_aliasing else f"FLAGGED ({len(sig_aliases)} aliases)"
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
    total_tests = len(test_results)
    pass_count = sum(1 for t in test_results.values() if t["status"] == "PASS")
    fail_count = sum(1 for t in test_results.values() if t["status"] == "FAIL")
    not_run_count = sum(1 for t in test_results.values() if t["status"] == "NOT_RUN")
    
    suite_passed = (total_tests > 0) and (fail_count == 0) and (not_run_count == 0)
    
    report["overall_summary"] = {
        "suite_passed": suite_passed,
        "total_tests": total_tests,
        "passed_tests": pass_count,
        "failed_tests": fail_count,
        "not_run_tests": not_run_count,
        "failures": failures
    }
    
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

    print("\n" + "=" * 76)
    print(f"SPECTRAL SUITE SUMMARY: {pass_count} / {total_tests} TESTS PASSED "
          f"({fail_count} Failed, {not_run_count} Not Run)")
    if not suite_passed:
        print("FAILURES DETECTED:")
        for fail in failures:
            print(f"  * {fail}")
    print(f"OVERALL RESULT: {'PASS' if suite_passed else 'FAIL'}")
    print("=" * 76)
    
    return 0 if suite_passed else 1

if __name__ == '__main__':
    out_directory = sys.argv[1] if len(sys.argv) > 1 else "out"
    sys.exit(run_spectral_suite(out_directory))
