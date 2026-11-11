#!/usr/bin/env python3
"""
DIMENSION5 — BBD CHARACTER ENGINE 2.1
Negative Test Suite for Consolidated Validation Summary Aggregator
Phase A.4 Sign-off

Verifies that tools/generate_validation_summary.py strictly rejects:
- Scenario A: Missing clock_accuracy_report.json -> NOT READY & exit != 0.
- Scenario B: Modified result set to FAIL -> NOT READY & exit != 0.
- Scenario C: Missing negative_validation_report.json -> NOT READY & exit != 0.
- Scenario D: Inconsistent count of executed vs passed tests -> NOT READY & exit != 0.
- Scenario E: Mismatched git commit in report -> NOT READY & exit != 0.
- Scenario F: Non-finite values (NaN / Inf) in report -> NOT READY & exit != 0.
"""

import os
import sys
import json
import shutil
import tempfile
import subprocess

def run_aggregator_negative_tests(golden_dir="out"):
    print("=" * 76)
    print("DIMENSION5 — VALIDATION SUMMARY AGGREGATOR NEGATIVE SUITE")
    print("=" * 76)

    aggregator_script = os.path.join(os.path.dirname(__file__), "generate_validation_summary.py")
    passed_tests = 0
    total_tests = 6

    # Verify that golden_dir has the valid reports to start from
    required = [
        "spectral_validation_report.json",
        "generator_precision_report.json",
        "clock_accuracy_report.json",
        "block_invariance_report.json",
        "negative_validation_report.json"
    ]
    
    for r in required:
        if not os.path.exists(os.path.join(golden_dir, r)):
            raise FileNotFoundError(f"Golden report missing: {os.path.join(golden_dir, r)}")

    def setup_temp_dir():
        tmp = tempfile.mkdtemp()
        for r in required:
            shutil.copy2(os.path.join(golden_dir, r), os.path.join(tmp, r))
        return tmp

    # -------------------------------------------------------------------------
    # Scenario A: Missing clock_accuracy_report.json -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[SCENARIO A] Missing clock_accuracy_report.json...")
    tmp_a = setup_temp_dir()
    try:
        os.remove(os.path.join(tmp_a, "clock_accuracy_report.json"))
        proc = subprocess.run([sys.executable, aggregator_script, tmp_a], capture_output=True, text=True)
        sum_path = os.path.join(tmp_a, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        if proc.returncode != 0 and status_not_ready:
            print("  --> PASS: Rejected missing clock accuracy report (NOT READY, exit!=0)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Did not reject missing clock report! returncode={proc.returncode}")
    finally:
        shutil.rmtree(tmp_a)

    # -------------------------------------------------------------------------
    # Scenario B: Modify a Suite Result to FAIL -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[SCENARIO B] Modified Suite Result to FAIL...")
    tmp_b = setup_temp_dir()
    try:
        clock_path = os.path.join(tmp_b, "clock_accuracy_report.json")
        with open(clock_path, 'r') as f:
            rep = json.load(f)
        rep["summary"]["all_tests_passed"] = False
        rep["measured_metrics"]["status"] = "FAIL"
        with open(clock_path, 'w') as f:
            json.dump(rep, f, indent=2)
            
        proc = subprocess.run([sys.executable, aggregator_script, tmp_b], capture_output=True, text=True)
        sum_path = os.path.join(tmp_b, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        if proc.returncode != 0 and status_not_ready:
            print("  --> PASS: Rejected suite failure (NOT READY, exit!=0)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Accepted suite failure! returncode={proc.returncode}")
    finally:
        shutil.rmtree(tmp_b)

    # -------------------------------------------------------------------------
    # Scenario C: Missing negative_validation_report.json -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[SCENARIO C] Missing negative_validation_report.json...")
    tmp_c = setup_temp_dir()
    try:
        os.remove(os.path.join(tmp_c, "negative_validation_report.json"))
        proc = subprocess.run([sys.executable, aggregator_script, tmp_c], capture_output=True, text=True)
        sum_path = os.path.join(tmp_c, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        if proc.returncode != 0 and status_not_ready:
            print("  --> PASS: Rejected missing negative validation report (NOT READY, exit!=0)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Did not reject missing negative validation report! returncode={proc.returncode}")
    finally:
        shutil.rmtree(tmp_c)

    # -------------------------------------------------------------------------
    # Scenario D: Inconsistent Count of Executed vs Passed Tests -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[SCENARIO D] Inconsistent Test Counts (total=10, passed=9, failed=0)...")
    tmp_d = setup_temp_dir()
    try:
        neg_path = os.path.join(tmp_d, "negative_validation_report.json")
        with open(neg_path, 'r') as f:
            rep = json.load(f)
        rep["overall_summary"]["passed_tests"] = 9
        rep["overall_summary"]["total_tests"] = 10
        rep["overall_summary"]["failed_tests"] = 0
        with open(neg_path, 'w') as f:
            json.dump(rep, f, indent=2)
            
        proc = subprocess.run([sys.executable, aggregator_script, tmp_d], capture_output=True, text=True)
        sum_path = os.path.join(tmp_d, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        if proc.returncode != 0 and status_not_ready:
            print("  --> PASS: Rejected inconsistent test counts (NOT READY, exit!=0)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Accepted inconsistent test counts! returncode={proc.returncode}")
    finally:
        shutil.rmtree(tmp_d)

    # -------------------------------------------------------------------------
    # Scenario E: Mismatched Git Commit in Report -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[SCENARIO E] Mismatched Git Commit (commit 'deadbeef')...")
    tmp_e = setup_temp_dir()
    try:
        invar_path = os.path.join(tmp_e, "block_invariance_report.json")
        with open(invar_path, 'r') as f:
            rep = json.load(f)
        rep["metadata"]["git_commit"] = "deadbeef12345678"
        with open(invar_path, 'w') as f:
            json.dump(rep, f, indent=2)
            
        proc = subprocess.run([sys.executable, aggregator_script, tmp_e], capture_output=True, text=True)
        sum_path = os.path.join(tmp_e, "validation_summary.json")
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
        shutil.rmtree(tmp_e)

    # -------------------------------------------------------------------------
    # Scenario F: Non-Finite Values (NaN / Inf) in Report -> NOT READY
    # -------------------------------------------------------------------------
    print("\n[SCENARIO F] Non-Finite NaN / Inf Value in Report...")
    tmp_f = setup_temp_dir()
    try:
        clock_path = os.path.join(tmp_f, "clock_accuracy_report.json")
        with open(clock_path, 'r') as f:
            rep = json.load(f)
        rep["summary"]["max_automation_relative_error"] = float('nan')
        rep["measured_metrics"]["max_automation_relative_error"] = float('nan')
        with open(clock_path, 'w') as f:
            json.dump(rep, f, indent=2)
            
        proc = subprocess.run([sys.executable, aggregator_script, tmp_f], capture_output=True, text=True)
        sum_path = os.path.join(tmp_f, "validation_summary.json")
        with open(sum_path, 'r') as f:
            summary = json.load(f)
        status_not_ready = (summary["metadata"]["overall_status"] == "NOT READY")
        if proc.returncode != 0 and status_not_ready:
            print("  --> PASS: Rejected non-finite NaN value (NOT READY, exit!=0)")
            passed_tests += 1
        else:
            print(f"  --> FAIL: Accepted NaN value! returncode={proc.returncode}")
    finally:
        shutil.rmtree(tmp_f)

    print("\n" + "=" * 76)
    print(f"SUMMARY AGGREGATOR NEGATIVE SUITE: {passed_tests} / {total_tests} SCENARIOS PASSED")
    print("=" * 76)
    return 0 if (passed_tests == total_tests) else 1

if __name__ == '__main__':
    golden_directory = sys.argv[1] if len(sys.argv) > 1 else "out"
    sys.exit(run_aggregator_negative_tests(golden_directory))
