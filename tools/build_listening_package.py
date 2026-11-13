#!/usr/bin/env python3
"""
Build the Phase B.1 listening-review package from the rendered A/B fixtures.

The audio files are copied byte-for-byte (float32 WAV, original sample rate and
amplitude). No per-version normalization or PCM16 conversion is performed.
"""

from __future__ import annotations

import datetime
import os
import shutil
import sys
import zipfile

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

from compare_phase_a4_b1 import (  # noqa: E402
    BASELINE_COMMIT,
    PHASE_BRANCH,
    REQUIRED_PAIRS,
    _git_head,
)

CATEGORY_TITLES = {
    "synth": "Synth Pluck",
    "guitar": "Clean Guitar (synthetic)",
    "pad": "Harmonic Pad",
    "stereo": "Stereo Asymmetric",
    "wet_only": "Wet-Only Pad",
}


def _char_tag(character: float) -> str:
    return "char100" if character >= 0.999 else "char050"


def main(argv: list[str]) -> int:
    out_dir = argv[1] if len(argv) > 1 else "out"
    review_dir = os.path.join(out_dir, "listening_review")
    if os.path.isdir(review_dir):
        shutil.rmtree(review_dir)
    os.makedirs(review_dir, exist_ok=True)

    missing = []
    copied = []
    for pair in REQUIRED_PAIRS:
        source_a4 = os.path.join(out_dir, pair["a4"])
        source_b1 = os.path.join(out_dir, pair["b1"])
        for source in (source_a4, source_b1):
            if not os.path.isfile(source):
                missing.append(source)

    if missing:
        print("FAIL — LISTENING_PACKAGE_SOURCE_MISSING")
        for source in missing:
            print(f"  missing: {source}")
        return 1

    for pair in REQUIRED_PAIRS:
        category = pair["category"]
        target_dir = os.path.join(review_dir, category)
        os.makedirs(target_dir, exist_ok=True)
        tag = _char_tag(float(pair["character"]))
        targets = {
            "a4": os.path.join(target_dir, f"phase_a4_{tag}_f32.wav"),
            "b1": os.path.join(target_dir, f"phase_b1_{tag}_f32.wav"),
        }
        shutil.copy2(os.path.join(out_dir, pair["a4"]), targets["a4"])
        shutil.copy2(os.path.join(out_dir, pair["b1"]), targets["b1"])
        copied.append((category, tag, targets))

    lines = [
        "# Dimension5 — Phase B.1 Listening Review Package",
        "",
        f"Generated (UTC): {datetime.datetime.now(datetime.timezone.utc).isoformat()}",
        f"Phase A.4 baseline commit: `{BASELINE_COMMIT}`",
        f"Phase B.1 branch: `{PHASE_BRANCH}`",
        f"Phase B.1 commit: `{_git_head()}`",
        "",
        "## Contents",
        "",
        "Every category contains 50% and 100% BBD Character comparisons:",
        "",
        "| Category | Character | A.4 file | B.1 file |",
        "|---|---|---|---|",
    ]
    for pair in REQUIRED_PAIRS:
        tag = _char_tag(float(pair["character"]))
        category = pair["category"]
        lines.append(
            f"| {CATEGORY_TITLES.get(category, category)} | {int(round(float(pair['character']) * 100))}% "
            f"| `{category}/phase_a4_{tag}_f32.wav` | `{category}/phase_b1_{tag}_f32.wav` |"
        )

    lines += [
        "",
        "## Playback and comparison notes",
        "",
        "- Format: IEEE float32 WAV, stereo, 48 kHz, 4 seconds.",
        "- The files are copied byte-for-byte from the renderer output. Versions were not",
        "  normalized independently and were not converted to PCM16.",
        "- Match loudness only on a per-pair basis if needed for casual listening; the",
        "  report JSON preserves the measured level difference without any per-version gain.",
        "- `guitar/` uses a synthetic clean-guitar style signal, not a real guitar recording.",
        "  The synthetic fixtures exercise pluck attacks, sustained harmonics and stereo",
        "  asymmetry, but they do not reproduce all behavior of a real recorded instrument.",
        "- The audible difference under review is the Phase B.1 clock-tracking spectral",
        "  character of the BBD pre-filter and reconstruction filter.",
        "",
    ]

    readme_path = os.path.join(review_dir, "README.md")
    with open(readme_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))

    zip_path = os.path.join(out_dir, "listening_review.zip")
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for root, _dirs, files in os.walk(review_dir):
            for filename in sorted(files):
                path = os.path.join(root, filename)
                arcname = os.path.join("listening_review", os.path.relpath(path, review_dir))
                archive.write(path, arcname)

    size_mb = os.path.getsize(zip_path) / (1024.0 * 1024.0)
    print(f"PASS — listening package: {len(copied)} A/B pairs")
    print(f"Directory: {review_dir}")
    print(f"ZIP: {zip_path} ({size_mb:.1f} MiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
