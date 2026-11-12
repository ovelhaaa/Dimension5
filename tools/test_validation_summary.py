#!/usr/bin/env python3
"""
DIMENSION5 — BBD CHARACTER ENGINE 2.1
Negative Test Suite for Consolidated Validation Summary Aggregator
Phase A.4 Sign-off

Verifies that tools/generate_validation_summary.py strictly rejects:
1. Missing A/B regression report -> NOT READY & exit != 0.
2. Malformed A/B regression report -> NOT READY & exit != 0.
3. Missing mandatory field in A/B report -> NOT READY & exit != 0.
4. Boundary continuity failure above 15 Hz -> NOT READY & exit != 0.
5. Metric out of bounds with status artificially set to PASS -> NOT READY & exit != 0.
6. Non-finite values (NaN / Inf) in report -> NOT READY & exit != 0.
7. Real A/B regression failure -> NOT READY & exit != 0.
8. Inconsistent invariance contract (status PASS with violation) -> NOT READY & exit != 0.
9. Missing clock accuracy report -> NOT READY & exit != 0.
10. Inconsistent test counts in negative report -> NOT READY & exit != 0.
11. Mismatched git commit in report -> NOT READY & exit != 0.
"""

import os
import sys
import json
import shutil
import tempfile
import subprocess

def run_aggregator_negative_tests(golden_dir="out"):
    print("=" * 76)
    print("DIMENSION5 — VALIDATION SUMMARY AGGREGATOR NEGATIVE SUITE (PHASE A.4)")
    print("=" * 76)

    aggregator_script = os.path.join(os.path.dirname(__file__), "generate_validation_summary.py")
    passed_tests = 0
    total_tests = 11

    # Verify that golden_dir has the valid reports to start from
    required = [
        "spectral_validation_report.json",
        "generator_precision_report.json",
        "clock_accuracy_report.json",
        "block_invariance_report.json",
        "negative_validation_report.json",
        "phase_a3_a4_regression_report.json"
    ]
    
    for r in required:
        path = os.path.join(golden_dir, r)
        if not os.path.exists(path):
            raise FileNotFoundError(f"Golden report missing: {path}")

    def setup_temp_dir():
        tmp = tempfile.mkdtemp()
        for r in required:
            shutil.copy2(os.path.join(golden_dir, r), os.path.join(tmp, r))
        return tmp

    # -------------------------------------------------------------------------
    # TEST 1: Missing A/B Regression Report -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 1] Missing A/B regression report (phase_a3_a4_regression_report.json)...")
    tmp1 = setup_temp_dir()
    try:
        os.remove(os.path.join(tmp1, "phase_a3_a4_regression_report.json"))
        proc = subprocess.run([sys.executable, aggregator_script, tmp1], capture_output=True, text=True)
        sum_path = os.path.join(tmp1, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        has_expected_err = any("MISSING_REPORT: phase_a3_a4_regression_report.json" in err for err in summary["metadata"].get("validation_errors", []))
        if proc.returncode != 0 and status_not_ready and has_expected_err:
            print("  --> PASS: Rejected missing A/B regression report (NOT READY, exit!=0, error flagged)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Did not reject missing A/B report! code={proc.returncode}, not_ready={status_not_ready}, err={has_expected_err}")
    finally:
        shutil.rmtree(tmp1)

    # -------------------------------------------------------------------------
    # TEST 2: Malformed A/B Regression Report -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 2] Malformed A/B regression report (corrupted JSON syntax)...")
    tmp2 = setup_temp_dir()
    try:
        reg_path = os.path.join(tmp2, "phase_a3_a4_regression_report.json")
        with open(reg_path, 'w') as f:
            f.write("{ invalid json content: [ truncated ...")
        proc = subprocess.run([sys.executable, aggregator_script, tmp2], capture_output=True, text=True)
        sum_path = os.path.join(tmp2, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        has_expected_err = any("INVALID_JSON: phase_a3_a4_regression_report.json" in err for err in summary["metadata"].get("validation_errors", []))
        if proc.returncode != 0 and status_not_ready and has_expected_err:
            print("  --> PASS: Rejected malformed A/B report (NOT READY, exit!=0, INVALID_JSON)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Did not reject malformed A/B report! code={proc.returncode}, not_ready={status_not_ready}, err={has_expected_err}")
    finally:
        shutil.rmtree(tmp2)

    # -------------------------------------------------------------------------
    # TEST 3: Mandatory Field in A/B Report Missing -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 3] Mandatory field in A/B report missing (removed steady_state from character_50pct)...")
    tmp3 = setup_temp_dir()
    try:
        reg_path = os.path.join(tmp3, "phase_a3_a4_regression_report.json")
        with open(reg_path, 'r') as f:
            rep = json.load(f)
        del rep["scenarios"]["character_50pct"]["steady_state"]
        with open(reg_path, 'w') as f:
            json.dump(rep, f, indent=2)
        proc = subprocess.run([sys.executable, aggregator_script, tmp3], capture_output=True, text=True)
        sum_path = os.path.join(tmp3, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        has_expected_err = any("missing 'steady_state' section" in err for err in summary["metadata"].get("validation_errors", []))
        if proc.returncode != 0 and status_not_ready and has_expected_err:
            print("  --> PASS: Rejected incomplete A/B report (NOT READY, exit!=0, missing field flagged)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Did not reject incomplete A/B report! code={proc.returncode}, not_ready={status_not_ready}, err={has_expected_err}")
    finally:
        shutil.rmtree(tmp3)

    # -------------------------------------------------------------------------
    # TEST 4: Boundary Continuity Failure (> 15 Hz) -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 4] Boundary continuity failure (step=20.0 Hz, status=FAIL)...")
    tmp4 = setup_temp_dir()
    try:
        invar_path = os.path.join(tmp4, "block_invariance_report.json")
        with open(invar_path, 'r') as f:
            rep = json.load(f)
        rep["boundary_continuity"]["max_single_sample_clock_step_hz"] = 20.0
        rep["boundary_continuity"]["status"] = "FAIL"
        with open(invar_path, 'w') as f:
            json.dump(rep, f, indent=2)
        proc = subprocess.run([sys.executable, aggregator_script, tmp4], capture_output=True, text=True)
        sum_path = os.path.join(tmp4, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        has_expected_err = any("boundary continuity failed" in err for err in summary["metadata"].get("validation_errors", []))
        if proc.returncode != 0 and status_not_ready and has_expected_err:
            print("  --> PASS: Rejected boundary continuity failure (NOT READY, exit!=0)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Accepted boundary continuity failure! code={proc.returncode}, not_ready={status_not_ready}, err={has_expected_err}")
    finally:
        shutil.rmtree(tmp4)

    # -------------------------------------------------------------------------
    # TEST 5: Metric Out of Bounds with Status Artificially PASS -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 5] Metric out of bounds with status artificially set to PASS (step=20.0 Hz, status=PASS)...")
    tmp5 = setup_temp_dir()
    try:
        invar_path = os.path.join(tmp5, "block_invariance_report.json")
        with open(invar_path, 'r') as f:
            rep = json.load(f)
        rep["boundary_continuity"]["max_single_sample_clock_step_hz"] = 20.0
        rep["boundary_continuity"]["status"] = "PASS"  # Artificially set to PASS
        with open(invar_path, 'w') as f:
            json.dump(rep, f, indent=2)
        proc = subprocess.run([sys.executable, aggregator_script, tmp5], capture_output=True, text=True)
        sum_path = os.path.join(tmp5, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        has_expected_err = any("INCONSISTENT_METRIC" in err for err in summary["metadata"].get("validation_errors", []))
        if proc.returncode != 0 and status_not_ready and has_expected_err:
            print("  --> PASS: Detected metric out of bounds with artificial PASS status (NOT READY, exit!=0, INCONSISTENT_METRIC)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Failed to detect inconsistency! code={proc.returncode}, not_ready={status_not_ready}, err={has_expected_err}")
    finally:
        shutil.rmtree(tmp5)

    # -------------------------------------------------------------------------
    # TEST 6: Non-Finite Values (NaN / Inf) in Required Report -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 6] Non-finite NaN value in required report (phase_a3_a4_regression_report.json)...")
    tmp6 = setup_temp_dir()
    try:
        reg_path = os.path.join(tmp6, "phase_a3_a4_regression_report.json")
        with open(reg_path, 'r') as f:
            rep = json.load(f)
        rep["scenarios"]["character_50pct"]["overall"]["rms_diff"] = float('nan')
        with open(reg_path, 'w') as f:
            json.dump(rep, f, indent=2)
        proc = subprocess.run([sys.executable, aggregator_script, tmp6], capture_output=True, text=True)
        sum_path = os.path.join(tmp6, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        has_expected_err = any("NON_FINITE_VALUE: phase_a3_a4_regression_report.json" in err for err in summary["metadata"].get("validation_errors", []))
        if proc.returncode != 0 and status_not_ready and has_expected_err:
            print("  --> PASS: Rejected non-finite NaN value in report (NOT READY, exit!=0, NON_FINITE_VALUE)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Accepted non-finite value! code={proc.returncode}, not_ready={status_not_ready}, err={has_expected_err}")
    finally:
        shutil.rmtree(tmp6)

    # -------------------------------------------------------------------------
    # TEST 7: Real A/B Regression Failure -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 7] Real A/B regression failure (steady_state_identical=False, verdict=FAIL)...")
    tmp7 = setup_temp_dir()
    try:
        reg_path = os.path.join(tmp7, "phase_a3_a4_regression_report.json")
        with open(reg_path, 'r') as f:
            rep = json.load(f)
        rep["conclusions"]["steady_state_identical"] = False
        rep["conclusions"]["verdict"] = "FAIL - Steady-state divergence detected"
        with open(reg_path, 'w') as f:
            json.dump(rep, f, indent=2)
        proc = subprocess.run([sys.executable, aggregator_script, tmp7], capture_output=True, text=True)
        sum_path = os.path.join(tmp7, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        has_expected_err = any("steady_state_identical is False" in err for err in summary["metadata"].get("validation_errors", []))
        if proc.returncode != 0 and status_not_ready and has_expected_err:
            print("  --> PASS: Rejected A/B regression failure (NOT READY, exit!=0, regression failure flagged)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Accepted A/B regression failure! code={proc.returncode}, not_ready={status_not_ready}, err={has_expected_err}")
    finally:
        shutil.rmtree(tmp7)

    # -------------------------------------------------------------------------
    # TEST 8: Inconsistent Invariance Contract -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 8] Inconsistent invariance contract (host MaxDiff=0.005 with status artificially PASS)...")
    tmp8 = setup_temp_dir()
    try:
        invar_path = os.path.join(tmp8, "block_invariance_report.json")
        with open(invar_path, 'r') as f:
            rep = json.load(f)
        rep["measured_metrics"]["host_segmentation_max_abs_diff"] = 0.005  # Host must be bit-exact (0.0)
        rep["measured_metrics"]["status"] = "PASS"
        with open(invar_path, 'w') as f:
            json.dump(rep, f, indent=2)
        proc = subprocess.run([sys.executable, aggregator_script, tmp8], capture_output=True, text=True)
        sum_path = os.path.join(tmp8, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        has_expected_err = any("INCONSISTENT_METRIC" in err or "host segmentation not bit-exact" in err for err in summary["metadata"].get("validation_errors", []))
        if proc.returncode != 0 and status_not_ready and has_expected_err:
            print("  --> PASS: Rejected invariance contract violation (NOT READY, exit!=0, inconsistency detected)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Accepted invariance contract violation! code={proc.returncode}, not_ready={status_not_ready}, err={has_expected_err}")
    finally:
        shutil.rmtree(tmp8)

    # -------------------------------------------------------------------------
    # TEST 9: Missing Clock Accuracy Report -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 9] Missing clock_accuracy_report.json...")
    tmp9 = setup_temp_dir()
    try:
        os.remove(os.path.join(tmp9, "clock_accuracy_report.json"))
        proc = subprocess.run([sys.executable, aggregator_script, tmp9], capture_output=True, text=True)
        sum_path = os.path.join(tmp9, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        if proc.returncode != 0 and status_not_ready:
            print("  --> PASS: Rejected missing clock accuracy report (NOT READY, exit!=0)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Did not reject missing clock report! returncode={proc.returncode}")
    finally:
        shutil.rmtree(tmp9)

    # -------------------------------------------------------------------------
    # TEST 10: Inconsistent Count of Executed vs Passed Tests -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 10] Inconsistent test counts (total=10, passed=9, failed=0)...")
    tmp10 = setup_temp_dir()
    try:
        neg_path = os.path.join(tmp10, "negative_validation_report.json")
        with open(neg_path, 'r') as f:
            rep = json.load(f)
        rep["overall_summary"]["passed_tests"] = 9
        rep["overall_summary"]["total_tests"] = 10
        rep["overall_summary"]["failed_tests"] = 0
        with open(neg_path, 'w') as f:
            json.dump(rep, f, indent=2)
        proc = subprocess.run([sys.executable, aggregator_script, tmp10], capture_output=True, text=True)
        sum_path = os.path.join(tmp10, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        if proc.returncode != 0 and status_not_ready:
            print("  --> PASS: Rejected inconsistent test counts (NOT READY, exit!=0)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Accepted inconsistent test counts! returncode={proc.returncode}")
    finally:
        shutil.rmtree(tmp10)

    # -------------------------------------------------------------------------
    # TEST 11: Mismatched Git Commit in Report -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[TEST 11] Mismatched git commit (commit 'deadbeef')...")
    tmp11 = setup_temp_dir()
    try:
        invar_path = os.path.join(tmp11, "block_invariance_report.json")
        with open(invar_path, 'r') as f:
            rep = json.load(f)
        rep["metadata"]["git_commit"] = "deadbeef12345678"
        with open(invar_path, 'w') as f:
            json.dump(rep, f, indent=2)
        proc = subprocess.run([sys.executable, aggregator_script, tmp11], capture_output=True, text=True)
        sum_path = os.path.join(tmp11, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        has_commit_err = any("COMMIT_MISMATCH" in err for err in summary["metadata"].get("validation_errors", []))
        if proc.returncode != 0 and status_not_ready and has_commit_err:
            print("  --> PASS: Flagged commit mismatch and rejected (NOT READY, exit!=0)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Accepted mismatched git commit! returncode={proc.returncode}")
    finally:
        shutil.rmtree(tmp11)

    print("\n" + "=" * 76)
    print(f"SUMMARY AGGREGATOR NEGATIVE SUITE: {passed_tests} / {total_tests} SCENARIOS PASSED")
    print("=" * 76)
    return 0 if (passed_tests == total_tests) else 1

if __name__ == '__main__':
    golden_directory = sys.argv[1] if len(sys.argv) > 1 else "out"
    sys.exit(run_aggregator_negative_tests(golden_directory))

if __name__ == '__main__':
    golden_directory = sys.argv[1] if len(sys.argv) > 1 else "out"
    sys.exit(run_aggregator_negative_tests(golden_directory))
