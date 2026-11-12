#!/usr/bin/env python3
"""
DIMENSION5 — BBD CHARACTER ENGINE 2.1
High-Resolution Spectral Validation & Aliasing Characterization Engine
Phase A.2 Validation Suite

Rigorous, FFT-based spectral analysis separating legitimate LFO modulation sidebands
(at fc +/- k * fm) from spurious components, harmonic distortion, and potential aliasing.
Supports float32 and PCM16 stereo/mono WAV fixtures across 44.1, 48, and 96 kHz.
"""

import math
import struct
import json
import csv
import os
import sys
import numpy as np
import scipy.signal as sig
import scipy.fft as fft
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def write_wav_float32(path, left, right, sample_rate):
    """Writes stereo 32-bit float WAV file."""
    left = np.asarray(left, dtype=np.float32)
    right = np.asarray(right, dtype=np.float32)
    n = min(len(left), len(right))
    interleaved = np.empty(2 * n, dtype=np.float32)
    interleaved[0::2] = left[:n]
    interleaved[1::2] = right[:n]
    raw_data = interleaved.tobytes()
    data_size = len(raw_data)
    
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sI4s', b'RIFF', 36 + data_size, b'WAVE'))
        byte_rate = sample_rate * 8
        f.write(struct.pack('<4sIHHIIHH', b'fmt ', 16, 3, 2, sample_rate, byte_rate, 8, 32))
        f.write(struct.pack('<4sI', b'data', data_size))
        f.write(raw_data)

def validate_wav_file(path, expected_sr=None, min_samples=None, expected_channels=2, require_float32=True):
    """
    Validates WAV file existence, header integrity, sample rate, channels, minimum duration,
    and absence of NaN/Inf.
    Returns:
        (is_valid: bool, error_code: str, left: np.ndarray, right: np.ndarray, sr: int)
    """
    if not os.path.exists(path):
        return False, "REQUIRED_FIXTURE_MISSING", None, None, 0
    try:
        with open(path, 'rb') as f:
            header = f.read(12)
            if len(header) < 12:
                return False, "INVALID_HEADER", None, None, 0
            riff, size, wave = struct.unpack('<4sI4s', header)
            if riff != b'RIFF' or wave != b'WAVE':
                return False, "INVALID_HEADER", None, None, 0
            
            fmt_found = False
            data_found = False
            audio_format = 1
            num_channels = 2
            sample_rate = 48000
            bits_per_sample = 16
            raw_data = None
            
            while True:
                chunk_header = f.read(8)
                if len(chunk_header) < 8:
                    break
                chunk_id, chunk_size = struct.unpack('<4sI', chunk_header)
                if chunk_id == b'fmt ':
                    fmt_data = f.read(chunk_size)
                    audio_format, num_channels, sample_rate, byte_rate, block_align, bits_per_sample = struct.unpack(
                        '<HHIIHH', fmt_data[:16]
                    )
                    fmt_found = True
                elif chunk_id == b'data':
                    raw_data = f.read(chunk_size)
                    data_found = True
                    break
                else:
                    f.seek(chunk_size, os.SEEK_CUR)
                    
            if not fmt_found or not data_found:
                return False, "INVALID_HEADER", None, None, 0
                
            if require_float32 and audio_format != 3:
                return False, "UNSUPPORTED_FORMAT", None, None, sample_rate
            if expected_channels is not None and num_channels != expected_channels:
                return False, "CHANNEL_COUNT_MISMATCH", None, None, sample_rate
            if expected_sr is not None and sample_rate != expected_sr:
                return False, "SAMPLE_RATE_MISMATCH", None, None, sample_rate
                
            if audio_format == 3:
                samples = np.frombuffer(raw_data, dtype=np.float32)
            elif audio_format == 1 and bits_per_sample == 16:
                samples = np.frombuffer(raw_data, dtype=np.int16).astype(np.float32) * (1.0 / 32768.0)
            elif audio_format == 1 and bits_per_sample == 32:
                samples = np.frombuffer(raw_data, dtype=np.int32).astype(np.float32) * (1.0 / 2147483648.0)
            else:
                return False, "UNSUPPORTED_FORMAT", None, None, sample_rate
                
            if num_channels == 2:
                left = samples[0::2]
                right = samples[1::2]
            else:
                left = samples
                right = samples
                
            n_frames = len(left)
            if min_samples is not None and n_frames < min_samples:
                return False, "INVALID_DURATION", left, right, sample_rate
                
            if np.isnan(left).any() or np.isnan(right).any() or np.isinf(left).any() or np.isinf(right).any():
                return False, "NONFINITE_AUDIO", left, right, sample_rate
                
            return True, "OK", left, right, sample_rate
    except Exception as e:
        return False, f"EXCEPTION_{type(e).__name__}", None, None, 0

def read_wav(path):
    """Reads 16-bit PCM or 32-bit float WAV file into left, right numpy arrays and sample rate."""
    with open(path, 'rb') as f:
        riff, size, wave = struct.unpack('<4sI4s', f.read(12))
        if riff != b'RIFF' or wave != b'WAVE':
            raise ValueError(f"Not a valid WAV file: {path}")
        
        fmt_found = False
        data_found = False
        audio_format = 1
        num_channels = 2
        sample_rate = 48000
        bits_per_sample = 16
        raw_data = None
        
        while True:
            chunk_header = f.read(8)
            if len(chunk_header) < 8:
                break
            chunk_id, chunk_size = struct.unpack('<4sI', chunk_header)
            
            if chunk_id == b'fmt ':
                fmt_data = f.read(chunk_size)
                audio_format, num_channels, sample_rate, byte_rate, block_align, bits_per_sample = struct.unpack(
                    '<HHIIHH', fmt_data[:16]
                )
                fmt_found = True
            elif chunk_id == b'data':
                raw_data = f.read(chunk_size)
                data_found = True
                break
            else:
                f.seek(chunk_size, os.SEEK_CUR)
                
        if not fmt_found or not data_found:
            raise ValueError(f"Incomplete WAV file: {path}")
            
        if audio_format == 3: # 32-bit IEEE float
            samples = np.frombuffer(raw_data, dtype=np.float32)
        elif audio_format == 1 and bits_per_sample == 16: # 16-bit PCM
            samples = np.frombuffer(raw_data, dtype=np.int16).astype(np.float32) * (1.0 / 32768.0)
        elif audio_format == 1 and bits_per_sample == 32: # 32-bit PCM
            samples = np.frombuffer(raw_data, dtype=np.int32).astype(np.float32) * (1.0 / 2147483648.0)
        else:
            raise ValueError(f"Unsupported WAV format: format={audio_format}, bits={bits_per_sample}")
            
        if num_channels == 2:
            left = samples[0::2]
            right = samples[1::2]
        else:
            left = samples
            right = samples
            
        return left, right, sample_rate

def compute_spectrum(signal, sr, window_name='blackmanharris'):
    """
    Computes single-sided amplitude spectrum with exact window coherent gain normalization.
    Returns:
        freqs: frequencies in Hz (0 to sr/2)
        mag_db: amplitude in dBFS (0 dBFS = full scale 1.0 peak sine)
        linear_mag: linear amplitude
        n_fft: FFT length
        df: frequency resolution (bin spacing in Hz)
    """
    n = len(signal)
    if window_name == 'blackmanharris':
        win = sig.windows.blackmanharris(n)
    elif window_name == 'hann':
        win = sig.windows.hann(n)
    else:
        win = np.ones(n)
        
    coherent_gain = np.sum(win)
    windowed = signal * win
    
    spec = fft.rfft(windowed)
    freqs = fft.rfftfreq(n, d=1.0 / sr)
    
    linear_mag = (2.0 / coherent_gain) * np.abs(spec)
    linear_mag[0] *= 0.5 # DC bin does not double
    if n % 2 == 0:
        linear_mag[-1] *= 0.5 # Nyquist bin does not double
        
    eps = 1e-12
    mag_db = 20.0 * np.log10(np.maximum(linear_mag, eps))
    df = float(sr) / float(n)
    
    return freqs, mag_db, linear_mag, n, df

def analyze_carrier_and_sidebands(signal, sr, f_carrier, f_mod=0.25, max_delay_depth_ms=2.4, max_sidebands=8):
    """
    Separates the carrier and legitimate LFO modulation sidebands (fc +/- k * fm)
    from spurious energy and calculates key spectral metrics.
    
    Uses Carson's rule / FM deviation physics to establish the legitimate modulation band:
      Delta_f = f_carrier * (depth_ms * 0.001) * 4 * f_mod
    """
    freqs, mag_db, linear_mag, n_fft, df = compute_spectrum(signal, sr, window_name='blackmanharris')
    
    # 1. Physics-based Modulation Bandwidth (Carson's Rule)
    # Peak frequency deviation in Dimension chorus:
    delta_f_peak = f_carrier * (max_delay_depth_ms * 0.001) * (4.0 * f_mod)
    mod_bandwidth_half = max(15.0, 1.5 * delta_f_peak + 4.0 * f_mod)
    
    mod_zone_low = max(0.0, f_carrier - mod_bandwidth_half)
    mod_zone_high = min(sr * 0.5, f_carrier + mod_bandwidth_half)
    
    # 2. Find exact carrier
    search_width_hz = max(2.0, 4.0 * df)
    carrier_mask = np.abs(freqs - f_carrier) <= search_width_hz
    carrier_bins = np.where(carrier_mask)[0]
    
    if len(carrier_bins) == 0:
        actual_carrier_bin = np.argmin(np.abs(freqs - f_carrier))
    else:
        actual_carrier_bin = carrier_bins[np.argmax(mag_db[carrier_bins])]
        
    measured_f_carrier = freqs[actual_carrier_bin]
    carrier_mag_linear = linear_mag[actual_carrier_bin]
    carrier_mag_db = mag_db[actual_carrier_bin]
    
    # Quadratic peak interpolation around carrier for sub-bin precision
    if 0 < actual_carrier_bin < len(mag_db) - 1:
        alpha = mag_db[actual_carrier_bin - 1]
        beta = mag_db[actual_carrier_bin]
        gamma = mag_db[actual_carrier_bin + 1]
        denom = (alpha - 2.0 * beta + gamma)
        if abs(denom) > 1e-6:
            p = 0.5 * (alpha - gamma) / denom
            measured_f_carrier += p * df
            carrier_mag_db = beta - 0.25 * (alpha - gamma) * p
            carrier_mag_linear = 10.0 ** (carrier_mag_db / 20.0)
            
    # Compute total fundamental power in the modulation zone
    in_mod_zone_mask = (freqs >= mod_zone_low) & (freqs <= mod_zone_high)
    total_mod_zone_power = np.sum(linear_mag[in_mod_zone_mask] ** 2)
    carrier_rms_dbfs = 10.0 * math.log10(max(1e-12, total_mod_zone_power))
    
    # 3. Extract discrete modulation sidebands: fc +/- k * fm
    sidebands = []
    total_sideband_power = 0.0
    half_bin_tolerance = max(1, int(round(0.4 * f_mod / df)))
    
    for k in range(1, max_sidebands + 1):
        for sign in [-1, +1]:
            target_f = measured_f_carrier + sign * k * f_mod
            if target_f < 0 or target_f >= sr * 0.5:
                continue
            target_bin = int(round(target_f / df))
            b_start = max(0, target_bin - half_bin_tolerance)
            b_end = min(len(linear_mag), target_bin + half_bin_tolerance + 1)
            sb_bin = b_start + np.argmax(linear_mag[b_start:b_end])
            
            sb_freq = freqs[sb_bin]
            sb_mag_lin = linear_mag[sb_bin]
            sb_mag_db = mag_db[sb_bin]
            sb_rel_dbc = sb_mag_db - carrier_mag_db
            
            sidebands.append({
                "order": k * sign,
                "target_freq_hz": float(target_f),
                "measured_freq_hz": float(sb_freq),
                "mag_dbfs": float(sb_mag_db),
                "rel_dbc": float(sb_rel_dbc),
                "mag_linear": float(sb_mag_lin)
            })
            total_sideband_power += sb_mag_lin * sb_mag_lin

    sideband_power_rel_dbc = 10.0 * math.log10(max(1e-12, total_sideband_power) / max(1e-12, carrier_mag_linear * carrier_mag_linear))
    
    # 4. Out-of-band spurious energy analysis
    out_of_band_mask = (freqs > 20.0) & ((freqs < mod_zone_low) | (freqs > mod_zone_high))
    
    # Mask out known harmonics of carrier (2*fc, 3*fc) up to Nyquist
    for h in range(2, 8):
        h_f = h * measured_f_carrier
        h_half_bw = max(10.0, h * mod_bandwidth_half)
        if h_f < sr * 0.5:
            out_of_band_mask &= ((freqs < h_f - h_half_bw) | (freqs > h_f + h_half_bw))
            
    out_of_band_bins = np.where(out_of_band_mask)[0]
    if len(out_of_band_bins) > 0:
        spurious_linear = linear_mag[out_of_band_bins]
        max_spurious_idx = np.argmax(spurious_linear)
        max_spurious_bin = out_of_band_bins[max_spurious_idx]
        max_spurious_freq = float(freqs[max_spurious_bin])
        max_spurious_dbfs = float(mag_db[max_spurious_bin])
        # Compare spurious level against total fundamental carrier power
        max_spurious_dbc = float(max_spurious_dbfs - carrier_rms_dbfs)
        
        # Estimated noise floor: median of out-of-band bins
        noise_floor_median_dbfs = float(np.median(mag_db[out_of_band_bins]))
        noise_floor_rms_dbfs = float(10.0 * math.log10(max(1e-12, np.mean(spurious_linear ** 2))))
        sfdr_db = float(carrier_rms_dbfs - max_spurious_dbfs)
    else:
        max_spurious_freq = 0.0
        max_spurious_dbfs = -120.0
        max_spurious_dbc = -120.0
        noise_floor_median_dbfs = -120.0
        noise_floor_rms_dbfs = -120.0
        sfdr_db = 120.0

    # 5. Potential Aliasing / Foldover Search
    # Check for folded components: f_alias = |k * sr - m * fc|
    aliasing_candidates = []
    for k in [1, 2]:
        for m in [1, 2, 3]:
            f_fold = abs(k * sr - m * f_carrier)
            if 20.0 < f_fold < (sr * 0.5 - 20.0):
                # Is f_fold distinct from carrier and harmonics?
                is_distinct = (abs(f_fold - f_carrier) > mod_bandwidth_half)
                for h in range(2, 6):
                    if abs(f_fold - h * f_carrier) < 30.0:
                        is_distinct = False
                if is_distinct:
                    # Search around f_fold with width m * mod_bandwidth_half
                    search_half = max(15.0, m * mod_bandwidth_half)
                    fold_mask = (freqs >= f_fold - search_half) & (freqs <= f_fold + search_half)
                    fold_bins = np.where(fold_mask)[0]
                    if len(fold_bins) > 0:
                        max_f_bin = fold_bins[np.argmax(mag_db[fold_bins])]
                        f_bin_actual = freqs[max_f_bin]
                        p_db = mag_db[max_f_bin]
                        rel_dbc = p_db - carrier_rms_dbfs
                        aliasing_candidates.append({
                            "fold_formula": f"|{k}*fs - {m}*fc|",
                            "predicted_freq_hz": float(f_fold),
                            "measured_freq_hz": float(f_bin_actual),
                            "mag_dbfs": float(p_db),
                            "rel_dbc": float(rel_dbc),
                            "is_significant": bool(rel_dbc > -60.0 and p_db > -80.0)
                        })

    return {
        "fft_length": int(n_fft),
        "freq_resolution_hz": float(df),
        "sample_rate": int(sr),
        "nominal_carrier_hz": float(f_carrier),
        "measured_carrier_hz": float(measured_f_carrier),
        "carrier_peak_dbfs": float(carrier_mag_db),
        "carrier_total_power_dbfs": float(carrier_rms_dbfs),
        "f_mod_hz": float(f_mod),
        "sidebands": sidebands,
        "sideband_power_rel_dbc": float(sideband_power_rel_dbc),
        "mod_zone_hz": [float(mod_zone_low), float(mod_zone_high)],
        "sfdr_db": float(sfdr_db),
        "max_spurious_freq_hz": float(max_spurious_freq),
        "max_spurious_dbfs": float(max_spurious_dbfs),
        "max_spurious_dbc": float(max_spurious_dbc),
        "noise_floor_median_dbfs": float(noise_floor_median_dbfs),
        "noise_floor_rms_dbfs": float(noise_floor_rms_dbfs),
        "aliasing_candidates": aliasing_candidates,
        "freqs": freqs,
        "mag_db": mag_db
    }

def classify_aliasing_components(test_candidates, baseline_candidates, df=0.0625, amp_tol_db=6.0, freq_tol_hz=None):
    """
    Rigorously compares detected aliasing/spurious components against baseline:
    - Matches components by frequency within tolerance (default max(5 * df, 10 Hz)).
    - Classifies each candidate as:
      * BASELINE_MATCHED: present in baseline with similar amplitude (|delta| <= amp_tol_db).
      * NEW_COMPONENT: significant component not present in baseline.
      * AMPLITUDE_INCREASE: present in baseline, but test level exceeds baseline by > amp_tol_db.
      * AMPLITUDE_DECREASE: present in baseline, but test level is lower by > amp_tol_db.
      * UNCERTAIN: candidate level below significance threshold.
    Returns:
      (classifications: list[dict], new_components_count: int, amplitude_increases_count: int)
    """
    freq_tol = freq_tol_hz if freq_tol_hz is not None else max(5.0 * df, 10.0)
    classifications = []
    new_components_count = 0
    amplitude_increases_count = 0

    base_matched_indices = set()

    for cand in test_candidates:
        f_cand = cand["measured_freq_hz"]
        p_cand = cand["mag_dbfs"]
        rel_cand = cand["rel_dbc"]
        is_sig = cand.get("is_significant", False) or (rel_cand > -60.0 and p_cand > -80.0)

        if not is_sig:
            classifications.append({
                "candidate": cand,
                "classification": "UNCERTAIN",
                "reason": "Level below significance threshold (-60 dBc or -80 dBFS)"
            })
            continue

        # Look for matching baseline candidate within frequency tolerance
        best_match = None
        best_idx = -1
        min_f_diff = float('inf')

        for idx, base_cand in enumerate(baseline_candidates):
            f_base = base_cand["measured_freq_hz"]
            f_diff = abs(f_cand - f_base)
            if f_diff <= freq_tol and f_diff < min_f_diff:
                min_f_diff = f_diff
                best_match = base_cand
                best_idx = idx

        if best_match is not None:
            base_matched_indices.add(best_idx)
            delta_amp = rel_cand - best_match["rel_dbc"]
            if delta_amp > amp_tol_db:
                classif = "AMPLITUDE_INCREASE"
                amplitude_increases_count += 1
                reason = f"Component level increased by {delta_amp:+.1f} dB (>{amp_tol_db} dB threshold)"
            elif delta_amp < -amp_tol_db:
                classif = "AMPLITUDE_DECREASE"
                reason = f"Component level decreased by {delta_amp:+.1f} dB"
            else:
                classif = "BASELINE_MATCHED"
                reason = f"Frequency match within {min_f_diff:.2f} Hz, level delta {delta_amp:+.1f} dB"

            classifications.append({
                "candidate": cand,
                "matched_baseline": best_match,
                "freq_diff_hz": min_f_diff,
                "delta_dbc": delta_amp,
                "classification": classif,
                "reason": reason
            })
        else:
            classifications.append({
                "candidate": cand,
                "classification": "NEW_COMPONENT",
                "reason": f"No baseline component found within {freq_tol:.1f} Hz tolerance"
            })
            new_components_count += 1

    return classifications, new_components_count, amplitude_increases_count

def compare_block_invariance(ref, test, sr, name="invariance"):
    """
    Computes rigorous temporal invariance metrics between reference and test signals:
    - Max Absolute Difference
    - RMS Difference
    - Normalized RMS Error (NRMSE = RMS_diff / RMS_ref)
    - Relative Error
    - Peak Difference
    - Max Single-Sample Jump (|diff[n] - diff[n-1]|)
    """
    n = min(len(ref), len(test))
    r = ref[:n]
    t = test[:n]
    diff = np.abs(t - r)
    
    max_abs_diff = float(np.max(diff))
    rms_ref = float(np.sqrt(np.mean(r ** 2)))
    rms_diff = float(np.sqrt(np.mean(diff ** 2)))
    nrmse = float(rms_diff / max(1e-9, rms_ref))
    peak_diff = float(abs(np.max(np.abs(t)) - np.max(np.abs(r))))
    
    # Boundary / single-sample jump check
    sample_jumps = np.abs(diff[1:] - diff[:-1])
    max_jump = float(np.max(sample_jumps)) if len(sample_jumps) > 0 else 0.0
    
    return {
        "test_name": name,
        "total_samples": int(n),
        "sample_rate": int(sr),
        "max_abs_diff": max_abs_diff,
        "rms_ref": rms_ref,
        "rms_diff": rms_diff,
        "nrmse": nrmse,
        "peak_diff": peak_diff,
        "max_single_sample_jump": max_jump,
        "is_bit_exact": bool(max_abs_diff == 0.0)
    }

def plot_spectral_validation(results_dict, output_path, title):
    """
    Generates publication-quality dual-panel spectral visualization:
    Top: Full Nyquist spectrum (0 to fs/2) with carrier, harmonics, and noise floor.
    Bottom: High-resolution zoom around carrier (+/- 5 Hz) resolving 0.25 Hz sidebands.
    """
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))
    fig.suptitle(title, fontsize=13, fontweight='bold')
    
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd']
    
    for idx, (label, res) in enumerate(results_dict.items()):
        freqs = res["freqs"]
        mag_db = res["mag_db"]
        c = colors[idx % len(colors)]
        
        # Panel 1: Full Spectrum
        ax1.plot(freqs, mag_db, label=label, color=c, alpha=0.85, linewidth=1.0)
        
        # Panel 2: Zoomed around carrier
        fc = res["measured_carrier_hz"]
        zoom_span = 5.0 # +/- 5 Hz
        zoom_mask = (freqs >= fc - zoom_span) & (freqs <= fc + zoom_span)
        ax2.plot(freqs[zoom_mask] - fc, mag_db[zoom_mask], label=label, color=c, alpha=0.85, linewidth=1.2)
        
    ax1.set_title("Full Spectrum (0 to Nyquist)")
    ax1.set_xlabel("Frequency (Hz)")
    ax1.set_ylabel("Magnitude (dBFS)")
    ax1.set_ylim(-130, 5)
    ax1.grid(True, linestyle=':', alpha=0.6)
    ax1.legend(loc='upper right')
    
    ax2.set_title(f"High-Resolution Zoom around Carrier: Resolving Sidebands (Δf ≈ {results_dict[list(results_dict.keys())[0]]['freq_resolution_hz']:.4f} Hz)")
    ax2.set_xlabel("Offset from Carrier (Hz)")
    ax2.set_ylabel("Magnitude (dBFS)")
    ax2.set_xlim(-5.0, 5.0)
    ax2.grid(True, linestyle=':', alpha=0.6)
    
    # Mark expected sideband ticks
    ax2.set_xticks(np.arange(-5.0, 5.1, 0.5))
    for tick in np.arange(-4.0, 4.1, 0.25):
        if abs(tick) > 0.01:
            ax2.axvline(tick, color='gray', linestyle='--', alpha=0.3)
    ax2.legend(loc='upper right')
    
    plt.tight_layout()
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    plt.savefig(output_path, dpi=180)
    plt.close()

if __name__ == '__main__':
    print("Spectral Validation Module loaded successfully.")
