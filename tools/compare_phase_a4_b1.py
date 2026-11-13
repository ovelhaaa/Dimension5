#!/usr/bin/env python3
"""
tools/compare_phase_a4_b1.py

Mandatory comparative audio & spectral characterization:
Phase A.4 (static pre/post BBD filters) vs Phase B.1 (clock-tracking spectral character).

The ten A/B fixture pairs are REQUIRED. Missing, truncated, non-float32,
non-stereo, sample-rate-mismatched, length-mismatched or non-finite fixtures
make the comparison fail with a non-zero exit code.

Exports out/phase_a4_vs_phase_b1_report.json and supports:
    python tools/compare_phase_a4_b1.py out
    python tools/compare_phase_a4_b1.py --validate-report out/report.json
"""

from __future__ import annotations

import datetime
import json
import math
import os
import subprocess
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import correlate

REQUIRED_PAIRS = [
    {
        "key": "synth_char050",
        "category": "synth",
        "character": 0.50,
        "label": "Synth Pluck (50% Character)",
        "a4": "ab_phase_a4_char050_synth_f32.wav",
        "b1": "ab_phase_b1_char050_synth_f32.wav",
    },
    {
        "key": "synth_char100",
        "category": "synth",
        "character": 1.00,
        "label": "Synth Pluck (100% Character)",
        "a4": "ab_phase_a4_char100_synth_f32.wav",
        "b1": "ab_phase_b1_char100_synth_f32.wav",
    },
    {
        "key": "guitar_char050",
        "category": "guitar",
        "character": 0.50,
        "label": "Clean Guitar (50% Character)",
        "a4": "ab_phase_a4_char050_guitar_f32.wav",
        "b1": "ab_phase_b1_char050_guitar_f32.wav",
    },
    {
        "key": "guitar_char100",
        "category": "guitar",
        "character": 1.00,
        "label": "Clean Guitar (100% Character)",
        "a4": "ab_phase_a4_char100_guitar_f32.wav",
        "b1": "ab_phase_b1_char100_guitar_f32.wav",
    },
    {
        "key": "pad_char050",
        "category": "pad",
        "character": 0.50,
        "label": "Harmonic Pad (50% Character)",
        "a4": "ab_phase_a4_char050_pad_f32.wav",
        "b1": "ab_phase_b1_char050_pad_f32.wav",
    },
    {
        "key": "pad_char100",
        "category": "pad",
        "character": 1.00,
        "label": "Harmonic Pad (100% Character)",
        "a4": "ab_phase_a4_char100_pad_f32.wav",
        "b1": "ab_phase_b1_char100_pad_f32.wav",
    },
    {
        "key": "stereo_char050",
        "category": "stereo",
        "character": 0.50,
        "label": "Stereo Asymmetric (50% Character)",
        "a4": "ab_phase_a4_char050_stereo_f32.wav",
        "b1": "ab_phase_b1_char050_stereo_f32.wav",
    },
    {
        "key": "stereo_char100",
        "category": "stereo",
        "character": 1.00,
        "label": "Stereo Asymmetric (100% Character)",
        "a4": "ab_phase_a4_char100_stereo_f32.wav",
        "b1": "ab_phase_b1_char100_stereo_f32.wav",
    },
    {
        "key": "wet_char050",
        "category": "wet_only",
        "character": 0.50,
        "label": "Wet-Only Pad (50% Character)",
        "a4": "ab_phase_a4_char050_wet_f32.wav",
        "b1": "ab_phase_b1_char050_wet_f32.wav",
    },
    {
        "key": "wet_char100",
        "category": "wet_only",
        "character": 1.00,
        "label": "Wet-Only Pad (100% Character)",
        "a4": "ab_phase_a4_char100_wet_f32.wav",
        "b1": "ab_phase_b1_char100_wet_f32.wav",
    },
]

BASELINE_COMMIT = "37591d09ee32647910678400b95075d6d7118a87"
PHASE_BRANCH = "feature/bbd-spectral-character-phase-b1"
HF_BAND_HZ = (6000.0, 15000.0)
LOW_BAND_HZ = (20.0, 200.0)


class FixtureError(RuntimeError):
    pass


def _git_head() -> str:
    env_sha = os.environ.get("GITHUB_SHA")
    if env_sha:
        return env_sha.strip()
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except OSError:
        pass
    return "unknown"


def _safe_correlation(x: np.ndarray, y: np.ndarray) -> float:
    x = np.asarray(x, dtype=np.float64)
    y = np.asarray(y, dtype=np.float64)
    x = x - float(np.mean(x))
    y = y - float(np.mean(y))
    denom = math.sqrt(float(np.mean(x * x)) * float(np.mean(y * y)))
    if denom <= 1e-15:
        return 1.0 if np.allclose(x, y) else 0.0
    return float(np.mean(x * y) / denom)


def _band_ratio_db(a4: np.ndarray, b1: np.ndarray, sr: int, band: tuple[float, float]) -> tuple[float, float, float]:
    window = np.hanning(a4.shape[0])
    freqs = np.fft.rfftfreq(a4.shape[0], 1.0 / sr)
    mask = (freqs >= band[0]) & (freqs <= band[1])
    if not np.any(mask):
        return 0.0, 0.0, 0.0
    energy_a4 = 0.0
    energy_b1 = 0.0
    for channel in range(a4.shape[1]):
        fft_a4 = np.abs(np.fft.rfft(a4[:, channel] * window))
        fft_b1 = np.abs(np.fft.rfft(b1[:, channel] * window))
        energy_a4 += float(np.sum(fft_a4[mask] ** 2))
        energy_b1 += float(np.sum(fft_b1[mask] ** 2))
    variation_db = 10.0 * math.log10(max(1e-30, energy_b1) / max(1e-30, energy_a4))
    return energy_a4, energy_b1, variation_db


def _stereo_metrics(data: np.ndarray, sr: int) -> dict:
    left = data[:, 0].astype(np.float64)
    right = data[:, 1].astype(np.float64)
    mid = 0.5 * (left + right)
    side = 0.5 * (left - right)
    mid_energy = float(np.mean(mid * mid))
    side_energy = float(np.mean(side * side))

    window = np.hanning(data.shape[0])
    freqs = np.fft.rfftfreq(data.shape[0], 1.0 / sr)
    low_mask = (freqs >= LOW_BAND_HZ[0]) & (freqs <= LOW_BAND_HZ[1])
    low_mid_energy = 0.0
    low_side_energy = 0.0
    if np.any(low_mask):
        low_mid_energy = float(np.sum(np.abs(np.fft.rfft(mid * window)[low_mask]) ** 2))
        low_side_energy = float(np.sum(np.abs(np.fft.rfft(side * window)[low_mask]) ** 2))

    return {
        "stereo_correlation": round(_safe_correlation(left, right), 6),
        "mid_energy": round(mid_energy, 9),
        "side_energy": round(side_energy, 9),
        "side_mid_ratio": round(side_energy / max(1e-30, mid_energy), 6),
        "low_band_hz": list(LOW_BAND_HZ),
        "low_band_side_mid_ratio": round(low_side_energy / max(1e-30, low_mid_energy), 6),
    }


def _temporal_alignment(a4: np.ndarray, b1: np.ndarray) -> tuple[int, float]:
    mid_a4 = 0.5 * (a4[:, 0].astype(np.float64) + a4[:, 1].astype(np.float64))
    mid_b1 = 0.5 * (b1[:, 0].astype(np.float64) + b1[:, 1].astype(np.float64))
    if mid_a4.size < 2 or not np.any(mid_a4) or not np.any(mid_b1):
        return 0, 0.0
    corr = correlate(mid_a4, mid_b1, mode="full", method="fft")
    lag = int(np.argmax(corr) - (mid_b1.size - 1))
    peak = float(np.max(np.abs(corr)))
    norm = math.sqrt(float(np.dot(mid_a4, mid_a4)) * float(np.dot(mid_b1, mid_b1)))
    return lag, (peak / norm) if norm > 0.0 else 0.0


def _read_fixture(path: str) -> tuple[int, np.ndarray]:
    if not os.path.isfile(path):
        raise FixtureError(f"missing fixture: {path}")
    try:
        sr, data = wavfile.read(path)
    except Exception as exc:  # truncated / malformed WAV
        raise FixtureError(f"unreadable WAV: {path}: {exc}") from exc

    if data.dtype != np.float32:
        raise FixtureError(f"expected IEEE float32 WAV, got dtype {data.dtype}: {path}")
    if data.ndim != 2 or data.shape[1] != 2:
        raise FixtureError(f"expected stereo (2 channels), got shape {data.shape}: {path}")
    if data.shape[0] <= 0:
        raise FixtureError(f"empty WAV: {path}")
    if not np.all(np.isfinite(data)):
        raise FixtureError(f"NaN/Inf detected in float32 samples: {path}")
    return int(sr), data


def analyze_ab_pair(path_a4: str, path_b1: str, pair: dict) -> dict:
    sr_a4, a4 = _read_fixture(path_a4)
    sr_b1, b1 = _read_fixture(path_b1)

    same_sample_rate = sr_a4 == sr_b1
    same_channel_count = a4.shape[1] == b1.shape[1] == 2
    same_sample_count = a4.shape[0] == b1.shape[0]
    finite_samples = bool(np.all(np.isfinite(a4)) and np.all(np.isfinite(b1)))
    float32_format = a4.dtype == b1.dtype == np.float32

    if not same_sample_rate:
        raise FixtureError(f"sample rate mismatch: A.4={sr_a4} Hz, B.1={sr_b1} Hz ({pair['label']})")
    if not same_channel_count:
        raise FixtureError(f"channel mismatch: A.4={a4.shape[1]}, B.1={b1.shape[1]} ({pair['label']})")
    if not same_sample_count:
        raise FixtureError(f"sample count mismatch: A.4={a4.shape[0]}, B.1={b1.shape[0]} ({pair['label']})")
    if not finite_samples:
        raise FixtureError(f"non-finite samples ({pair['label']})")
    if not float32_format:
        raise FixtureError(f"non-float32 fixtures ({pair['label']})")

    a4_64 = a4.astype(np.float64)
    b1_64 = b1.astype(np.float64)

    peak_a4 = float(np.max(np.abs(a4_64)))
    peak_b1 = float(np.max(np.abs(b1_64)))
    rms_a4 = float(np.sqrt(np.mean(a4_64 * a4_64)))
    rms_b1 = float(np.sqrt(np.mean(b1_64 * b1_64)))

    diff = b1_64 - a4_64
    max_diff = float(np.max(np.abs(diff)))
    rms_diff = float(np.sqrt(np.mean(diff * diff)))
    gain_diff_db = 20.0 * math.log10(max(1e-30, rms_b1) / max(1e-30, rms_a4))
    peak_diff_db = 20.0 * math.log10(max(1e-30, peak_b1) / max(1e-30, peak_a4))

    corr_l = _safe_correlation(a4_64[:, 0], b1_64[:, 0])
    corr_r = _safe_correlation(a4_64[:, 1], b1_64[:, 1])
    cross_version_correlation = 0.5 * (corr_l + corr_r)

    lag, alignment_corr = _temporal_alignment(a4, b1)

    hf_energy_a4, hf_energy_b1, hf_variation_db = _band_ratio_db(a4_64, b1_64, sr_a4, HF_BAND_HZ)

    # A file independently peak-normalized to exactly full scale while its
    # counterpart is not is a red flag for differential normalization.
    fullscale_a4 = abs(peak_a4 - 1.0) <= 1e-6
    fullscale_b1 = abs(peak_b1 - 1.0) <= 1e-6
    normalization_suspect = bool(fullscale_a4 != fullscale_b1)

    # Technical sanity only: no uncontrolled gain change, same temporal
    # material, and same polarity/timbre family. These checks do not judge the
    # musical size of the spectral character difference.
    gain_sanity_ok = abs(gain_diff_db) <= 12.0
    temporal_alignment_ok = abs(lag) <= 256
    correlation_sanity_ok = cross_version_correlation > 0.0

    integrity = {
        "status": "PASS",
        "float32_format": True,
        "same_sample_rate": True,
        "same_channel_count": True,
        "same_sample_count": True,
        "finite_samples": True,
        "temporal_length_match": True,
        "differential_normalization_suspect": normalization_suspect,
        "gain_sanity_ok": gain_sanity_ok,
        "temporal_alignment_ok": temporal_alignment_ok,
        "correlation_sanity_ok": correlation_sanity_ok,
        "sample_rate_hz": sr_a4,
        "channels": 2,
        "samples": int(a4.shape[0]),
    }

    if normalization_suspect:
        raise FixtureError(
            f"differential normalization suspected (exactly one file at full scale): {pair['label']}"
        )
    if not (gain_sanity_ok and temporal_alignment_ok and correlation_sanity_ok):
        raise FixtureError(
            f"technical integrity check failed "
            f"(gain={gain_diff_db:+.3f} dB, lag={lag}, correlation={cross_version_correlation:.5f}): "
            f"{pair['label']}"
        )

    return {
        "fixture_key": pair["key"],
        "fixture_label": pair["label"],
        "category": pair["category"],
        "character": pair["character"],
        "sample_rate": sr_a4,
        "format": "WAV/IEEE float32",
        "integrity": integrity,
        "phase_a4": {
            "peak": round(peak_a4, 8),
            "rms": round(rms_a4, 8),
            "stereo_metrics": _stereo_metrics(a4, sr_a4),
        },
        "phase_b1": {
            "peak": round(peak_b1, 8),
            "rms": round(rms_b1, 8),
            "stereo_metrics": _stereo_metrics(b1, sr_b1),
        },
        "comparison": {
            "max_abs_diff": round(max_diff, 9),
            "rms_diff": round(rms_diff, 9),
            "rms_diff_dbfs": round(20.0 * math.log10(max(1e-30, rms_diff)), 3),
            "gain_diff_db": round(gain_diff_db, 4),
            "peak_diff_db": round(peak_diff_db, 4),
            "cross_version_correlation": round(cross_version_correlation, 7),
            "cross_version_correlation_l": round(corr_l, 7),
            "cross_version_correlation_r": round(corr_r, 7),
            "stereo_correlation_a4": round(_safe_correlation(a4_64[:, 0], a4_64[:, 1]), 7),
            "stereo_correlation_b1": round(_safe_correlation(b1_64[:, 0], b1_64[:, 1]), 7),
            "side_mid_ratio_a4": round(_stereo_metrics(a4, sr_a4)["side_mid_ratio"], 7),
            "side_mid_ratio_b1": round(_stereo_metrics(b1, sr_b1)["side_mid_ratio"], 7),
            "temporal_lag_samples": lag,
            "temporal_alignment_correlation": round(alignment_corr, 7),
            "hf_band_hz": list(HF_BAND_HZ),
            "hf_energy_a4": round(hf_energy_a4, 6),
            "hf_energy_b1": round(hf_energy_b1, 6),
            "hf_6k_15k_variation_db": round(hf_variation_db, 4),
        },
    }


REPORT_EVALUATION_FIELDS = {
    "fixture_key",
    "fixture_label",
    "category",
    "character",
    "sample_rate",
    "format",
    "integrity",
    "phase_a4",
    "phase_b1",
    "comparison",
}
REPORT_INTEGRITY_FIELDS = {
    "status",
    "float32_format",
    "same_sample_rate",
    "same_channel_count",
    "same_sample_count",
    "finite_samples",
    "temporal_length_match",
    "differential_normalization_suspect",
    "gain_sanity_ok",
    "temporal_alignment_ok",
    "correlation_sanity_ok",
    "sample_rate_hz",
    "channels",
    "samples",
}
REPORT_PHASE_FIELDS = {"peak", "rms", "stereo_metrics"}
REPORT_STEREO_FIELDS = {
    "stereo_correlation",
    "mid_energy",
    "side_energy",
    "side_mid_ratio",
    "low_band_hz",
    "low_band_side_mid_ratio",
}
REPORT_COMPARISON_FIELDS = {
    "max_abs_diff",
    "rms_diff",
    "rms_diff_dbfs",
    "gain_diff_db",
    "peak_diff_db",
    "cross_version_correlation",
    "cross_version_correlation_l",
    "cross_version_correlation_r",
    "stereo_correlation_a4",
    "stereo_correlation_b1",
    "side_mid_ratio_a4",
    "side_mid_ratio_b1",
    "temporal_lag_samples",
    "temporal_alignment_correlation",
    "hf_band_hz",
    "hf_energy_a4",
    "hf_energy_b1",
    "hf_6k_15k_variation_db",
}
REPORT_METADATA_FIELDS = {
    "title",
    "baseline_commit",
    "phase_b1_branch",
    "phase_b1_commit",
    "generated_utc",
    "pairs_required",
    "pairs_processed",
    "status",
}


def validate_report(report: dict) -> list[str]:
    errors: list[str] = []
    if not isinstance(report, dict):
        return ["report root is not a JSON object"]
    metadata = report.get("metadata")
    evaluations = report.get("evaluations")
    if not isinstance(metadata, dict):
        errors.append("missing or invalid 'metadata'")
        metadata = {}
    if not isinstance(evaluations, list):
        errors.append("missing or invalid 'evaluations'")
        evaluations = []

    missing_meta = REPORT_METADATA_FIELDS - set(metadata)
    if missing_meta:
        errors.append(f"metadata missing fields: {sorted(missing_meta)}")
    if metadata.get("pairs_required") != len(REQUIRED_PAIRS):
        errors.append(f"pairs_required must be {len(REQUIRED_PAIRS)}")
    if metadata.get("status") == "PASS":
        if metadata.get("pairs_processed") != len(REQUIRED_PAIRS):
            errors.append("PASS report must have all required pairs processed")
        if len(evaluations) != len(REQUIRED_PAIRS):
            errors.append("PASS report must contain ten evaluations")

    for index, evaluation in enumerate(evaluations):
        prefix = f"evaluations[{index}]"
        if not isinstance(evaluation, dict):
            errors.append(f"{prefix} is not an object")
            continue
        missing = REPORT_EVALUATION_FIELDS - set(evaluation)
        if missing:
            errors.append(f"{prefix} missing fields: {sorted(missing)}")
            continue
        integrity = evaluation["integrity"]
        if not isinstance(integrity, dict):
            errors.append(f"{prefix}.integrity is not an object")
        else:
            missing = REPORT_INTEGRITY_FIELDS - set(integrity)
            if missing:
                errors.append(f"{prefix}.integrity missing fields: {sorted(missing)}")
        for phase_key in ("phase_a4", "phase_b1"):
            phase = evaluation[phase_key]
            if not isinstance(phase, dict):
                errors.append(f"{prefix}.{phase_key} is not an object")
                continue
            missing = REPORT_PHASE_FIELDS - set(phase)
            if missing:
                errors.append(f"{prefix}.{phase_key} missing fields: {sorted(missing)}")
                continue
            stereo = phase["stereo_metrics"]
            if not isinstance(stereo, dict):
                errors.append(f"{prefix}.{phase_key}.stereo_metrics is not an object")
            else:
                missing = REPORT_STEREO_FIELDS - set(stereo)
                if missing:
                    errors.append(f"{prefix}.{phase_key}.stereo_metrics missing fields: {sorted(missing)}")
        comparison = evaluation["comparison"]
        if not isinstance(comparison, dict):
            errors.append(f"{prefix}.comparison is not an object")
        else:
            missing = REPORT_COMPARISON_FIELDS - set(comparison)
            if missing:
                errors.append(f"{prefix}.comparison missing fields: {sorted(missing)}")
    return errors


def _write_report(path: str, report: dict) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)


def _print_pair_result(result: dict) -> None:
    comparison = result["comparison"]
    print(f"[{result['fixture_label']}]")
    print(
        f"  Max Diff: {comparison['max_abs_diff']:.6f} | "
        f"RMS Diff: {comparison['rms_diff']:.6f} ({comparison['rms_diff_dbfs']} dBFS)"
    )
    print(
        f"  Level Change: {comparison['gain_diff_db']:+.3f} dB | "
        f"Cross Correlation: {comparison['cross_version_correlation']:.5f} | "
        f"Temporal Lag: {comparison['temporal_lag_samples']} samples"
    )
    print(
        f"  Stereo Side/Mid: A4={comparison['side_mid_ratio_a4']:.5f} -> "
        f"B1={comparison['side_mid_ratio_b1']:.5f}"
    )
    print(
        f"  HF (6k-15k) Spectral Change: {comparison['hf_6k_15k_variation_db']:+.3f} dB\n"
    )


def main(argv: list[str]) -> int:
    if len(argv) >= 3 and argv[1] == "--validate-report":
        try:
            with open(argv[2], "r", encoding="utf-8") as handle:
                report = json.load(handle)
        except Exception as exc:
            print(f"FAIL — REPORT_UNREADABLE: {exc}")
            return 1
        errors = validate_report(report)
        if errors:
            print("FAIL — REPORT_SCHEMA_INCOMPLETE")
            for error in errors:
                print(f"  - {error}")
            return 1
        print("PASS — report schema complete")
        return 0

    out_dir = argv[1] if len(argv) > 1 else "out"
    print("=" * 76)
    print("DIMENSION5 — PHASE A.4 vs PHASE B.1 MANDATORY A/B COMPARISON")
    print("=" * 76)

    missing = []
    for pair in REQUIRED_PAIRS:
        for key in ("a4", "b1"):
            path = os.path.join(out_dir, pair[key])
            if not os.path.isfile(path):
                missing.append(path)

    report = {
        "metadata": {
            "title": "Dimension5 Phase A.4 vs Phase B.1 Comparison",
            "baseline_commit": BASELINE_COMMIT,
            "phase_b1_branch": PHASE_BRANCH,
            "phase_b1_commit": _git_head(),
            "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "pairs_required": len(REQUIRED_PAIRS),
            "pairs_processed": 0,
            "status": "FAIL",
            "fixture_format": "WAV/IEEE float32, stereo, no PCM16 conversion",
        },
        "missing_fixtures": missing,
        "errors": [],
        "evaluations": [],
    }

    out_json = os.path.join(out_dir, "phase_a4_vs_phase_b1_report.json")

    if missing:
        print("FAIL — REQUIRED_AB_FIXTURE_MISSING")
        for path in missing:
            print(f"  missing: {path}")
        report["errors"].append("REQUIRED_AB_FIXTURE_MISSING")
        _write_report(out_json, report)
        print(f"Report saved to: {out_json}")
        return 1

    results = []
    errors = []
    for pair in REQUIRED_PAIRS:
        path_a4 = os.path.join(out_dir, pair["a4"])
        path_b1 = os.path.join(out_dir, pair["b1"])
        try:
            result = analyze_ab_pair(path_a4, path_b1, pair)
        except FixtureError as exc:
            errors.append(str(exc))
            print(f"FAIL — {exc}")
            continue
        results.append(result)
        _print_pair_result(result)

    report["evaluations"] = results
    report["metadata"]["pairs_processed"] = len(results)
    report["errors"] = errors

    if errors or len(results) != len(REQUIRED_PAIRS) or not results:
        report["metadata"]["status"] = "FAIL"
        print("FAIL — A/B FIXTURE INTEGRITY")
        for error in errors:
            print(f"  - {error}")
        _write_report(out_json, report)
        print(f"Report saved to: {out_json}")
        return 1

    report["metadata"]["status"] = "PASS"
    schema_errors = validate_report(report)
    if schema_errors:
        report["metadata"]["status"] = "FAIL"
        report["errors"] = schema_errors
        print("FAIL — REPORT_SCHEMA_INCOMPLETE")
        for error in schema_errors:
            print(f"  - {error}")
        _write_report(out_json, report)
        return 1

    _write_report(out_json, report)
    print(f"PASS — all {len(REQUIRED_PAIRS)} required A/B pairs validated")
    print(f"Report saved to: {out_json}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
