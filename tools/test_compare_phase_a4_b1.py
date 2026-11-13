#!/usr/bin/env python3
"""
Negative/robustness tests for tools/compare_phase_a4_b1.py.

These synthetic fixtures are test-only and are never written into the
production renderer output. Every corrupted case must make the comparator
exit with a non-zero status.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from scipy.io import wavfile

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
COMPARATOR = os.path.join(TOOLS_DIR, "compare_phase_a4_b1.py")

sys.path.insert(0, TOOLS_DIR)
from compare_phase_a4_b1 import REQUIRED_PAIRS  # noqa: E402

SR = 48000
FRAMES = 6000


def _signal(character: float, channel: int) -> np.ndarray:
    t = np.arange(FRAMES, dtype=np.float64) / SR
    base = 0.20 * np.sin(2.0 * np.pi * (440.0 + 120.0 * channel) * t)
    modified = base * (1.0 - 0.05 * character) + 0.01 * character * np.sin(
        2.0 * np.pi * (6000.0 + 500.0 * channel) * t
    )
    return modified


def _write_pair(out_dir: str, pair: dict) -> None:
    for version, character_scale in (("a4", 1.0), ("b1", 1.0)):
        character = float(pair["character"]) * character_scale
        data = np.stack(
            [_signal(character, 0), _signal(character, 1)], axis=1
        ).astype(np.float32)
        wavfile.write(os.path.join(out_dir, pair[version]), SR, data)


def make_valid_fixtures(out_dir: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    for pair in REQUIRED_PAIRS:
        _write_pair(out_dir, pair)


def run_comparator(out_dir: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, COMPARATOR, out_dir],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )


def expect_failure(name: str, out_dir: str, expected_token: str | None = None) -> None:
    result = run_comparator(out_dir)
    if result.returncode == 0:
        print(f"FAIL: {name} was unexpectedly accepted")
        print(result.stdout)
        raise SystemExit(1)
    if expected_token and expected_token not in result.stdout:
        print(f"FAIL: {name} failed without expected token {expected_token}")
        print(result.stdout)
        raise SystemExit(1)
    print(f"PASS: {name} rejected (exit {result.returncode})")


def expect_report_failure(name: str, report_path: str) -> None:
    result = subprocess.run(
        [sys.executable, COMPARATOR, "--validate-report", report_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if result.returncode == 0:
        print(f"FAIL: {name} was unexpectedly accepted")
        print(result.stdout)
        raise SystemExit(1)
    print(f"PASS: {name} rejected (exit {result.returncode})")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="dimension5_ab_neg_") as root:
        # Positive control: a complete valid set must pass before corrupting it.
        valid_dir = os.path.join(root, "valid")
        make_valid_fixtures(valid_dir)
        ok = run_comparator(valid_dir)
        if ok.returncode != 0:
            print("FAIL: valid positive-control fixture set was rejected")
            print(ok.stdout)
            return 1
        report_path = os.path.join(valid_dir, "phase_a4_vs_phase_b1_report.json")
        with open(report_path, "r", encoding="utf-8") as handle:
            report = json.load(handle)
        if report["metadata"]["status"] != "PASS" or len(report["evaluations"]) != 10:
            print("FAIL: valid positive-control report is incomplete")
            return 1
        print("PASS: valid positive-control set produced a complete PASS report")

        # 1. One of the ten pairs missing.
        case_dir = os.path.join(root, "missing_pair")
        shutil.copytree(valid_dir, case_dir)
        os.remove(os.path.join(case_dir, REQUIRED_PAIRS[3]["b1"]))
        expect_failure("missing required pair", case_dir, "REQUIRED_AB_FIXTURE_MISSING")

        # 2. Truncated WAV.
        case_dir = os.path.join(root, "truncated")
        shutil.copytree(valid_dir, case_dir)
        victim = os.path.join(case_dir, REQUIRED_PAIRS[0]["a4"])
        with open(victim, "rb") as handle:
            prefix = handle.read(96)
        with open(victim, "wb") as handle:
            handle.write(prefix)
        expect_failure("truncated WAV", case_dir, "unreadable WAV")

        # 3. Sample rate mismatch.
        case_dir = os.path.join(root, "sample_rate_mismatch")
        shutil.copytree(valid_dir, case_dir)
        pair = REQUIRED_PAIRS[2]
        data = np.stack(
            [_signal(0.5, 0), _signal(0.5, 1)], axis=1
        ).astype(np.float32)
        wavfile.write(os.path.join(case_dir, pair["b1"]), 44100, data)
        expect_failure("sample rate mismatch", case_dir, "sample rate mismatch")

        # 4. Incorrect channel count (mono).
        case_dir = os.path.join(root, "mono")
        shutil.copytree(valid_dir, case_dir)
        pair = REQUIRED_PAIRS[4]
        wavfile.write(
            os.path.join(case_dir, pair["b1"]), SR, _signal(0.5, 0).astype(np.float32)
        )
        expect_failure("mono channel fixture", case_dir, "expected stereo")

        # 5. NaN/Inf in a float32 fixture.
        for bad_value, label in ((np.nan, "NaN"), (np.inf, "Inf")):
            case_dir = os.path.join(root, f"nonfinite_{label.lower()}")
            shutil.copytree(valid_dir, case_dir)
            pair = REQUIRED_PAIRS[6]
            data = np.stack(
                [_signal(0.5, 0), _signal(0.5, 1)], axis=1
            ).astype(np.float32)
            data[17, 0] = bad_value
            wavfile.write(os.path.join(case_dir, pair["b1"]), SR, data)
            expect_failure(f"{label} float32 sample", case_dir, "NaN/Inf")

        # 6. No pair processed at all (empty output directory).
        case_dir = os.path.join(root, "empty")
        os.makedirs(case_dir, exist_ok=True)
        expect_failure("empty output directory", case_dir, "REQUIRED_AB_FIXTURE_MISSING")

        # 7. Incomplete JSON report schema.
        bad_report = os.path.join(root, "incomplete_report.json")
        with open(bad_report, "w", encoding="utf-8") as handle:
            json.dump({"metadata": {"title": "incomplete"}, "evaluations": []}, handle)
        expect_report_failure("incomplete report schema", bad_report)

    print("test_compare_phase_a4_b1: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
