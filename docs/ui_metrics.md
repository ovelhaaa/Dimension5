# Stereo monitor metrics (UI 2.1)

All values are derived from the real measured output of the DSP, unless the
card explicitly compares input and output (STEREO DELTA).

## Validity rules

| State | Display |
| --- | --- |
| No signal (both input and output below the analyzer floor) | `--` + `SILENT` |
| Correlation undefined (single active channel) | `--` + `LEFT ONLY` / `RIGHT ONLY` |
| Pure mono output (Side below the RMS floor) | `--` + `MONO` |
| Pure side / antiphase output (Mid below the floor) | `--` + `SIDE ONLY` |
| DELTA input mono or side-only | `N/A` + `FROM MONO` / `FROM SIDE` |
| DELTA output undefined | `N/A` + `SIDE ONLY` / `LIMITED` |

No `-inf`, `+inf` or fabricated finite value is shown for an undefined
measurement.

## Correlation thresholds

| Range | Label |
| --- | --- |
| `>= +0.60` | `IN PHASE` |
| `>= +0.15` | `CORRELATED` |
| `> -0.15` | `DECORRELATED` |
| `> -0.60` | `PHASE RISK` |
| `< -0.60` | `ANTIPHASE` |

Correlation is only evaluated when both output channels carry measurable
signal. A negative correlation alone is not presented as audible cancellation.

## Mono sum thresholds

`monoAttenuationDb = 20 * log10(monoRms / stereoReferenceRms)`.

| Range | Label |
| --- | --- |
| `>= -1.0 dB` | `STABLE` |
| `>= -3.5 dB` | `MILD LOSS` |
| `>= -7.0 dB` | `ATTENUATION` |
| `< -7.0 dB` | `CANCEL RISK` |

`STABLE` replaces the previous judgemental `OPTIMAL` label.

## Stereo delta

`stereoDeltaDb = outputSideMidDb - inputSideMidDb`.

| Condition | Label |
| --- | --- |
| `> +0.4 dB` | `EXPANDED` |
| `< -0.4 dB` | `NARROWED` |
| otherwise | `UNITY` |
| delta mathematically undefined | `N/A` + reason |

In bypass the card shows `BYPASS` and the measured value when it is valid. It
never forces `0.0 dB` for an invalid measurement and never claims that the
processor expanded the image while bypassed.

## Visual scale

The goniometer uses a display-only scalar gain, completely separate from the
audio gain:

- target peak: `0.92` in display units;
- maximum visual gain: `48x`;
- amplification disabled below the visual noise floor (`0.002`);
- reduction (loud signal) time constant: `55 ms`;
- recovery (quiet signal) time constant: `220 ms`;
- clamp: `0.45x .. 48x`.

The same scalar is applied to both Mid and Side axes, so the relative M/S
dispersion remains mathematically unchanged. In DELTA the same scalar is used
for the input and output goniometers; it is derived from the combined peak of
both so neither panel receives an independent, misleading zoom.

The field cloud uses a `200 ms` half-life:

```cpp
decay = std::exp(-std::log(2.0f) * deltaTime / 0.20f);
```
