# Dimension5 — Benchmark & Performance Analysis

This directory contains benchmarking tools and performance evaluations for the Dimension5 DSP core, with special focus on the **BBD Character Engine 2.1 (Phase A — Clock Modeling & Temporal Character)** and its execution budget on the **ARM Cortex-M33** (STM32H562 @ 250 MHz) as well as modern host desktop environments.

---

## 1. Static Memory Footprint

The Dimension5 DSP core is completely deterministic and static, allocating **zero bytes dynamically** on heap (`malloc`/`new`/`free`/`delete`).

| Component | Size (Bytes) | Notes |
| :--- | :--- | :--- |
| **`DimensionDSP` (Total Struct)** | **16,640 bytes (~16.3 kB)** | Entire core instance fits in STM32 SRAM |
| Delay Line Buffers (L & R) | 16,384 bytes (16.0 kB) | 2 × 2,048 samples × 4 bytes (`float32`) |
| Filter & Envelope States | ~176 bytes | HPF, LPFs, Compander, Spatial Engine |
| Parameter Smoothing State | ~64 bytes | Linear ramps per block |
| **`DimensionBBDClock` State** | **16 bytes** | 2 × 4B dispersion states + 2 × 4B clock history |

### Embedded Memory Suitability
- Target MCU: **STM32H562** (contains 640 kB internal SRAM: SRAM1, SRAM2, SRAM3).
- Total footprint represents **< 2.6%** of internal MCU RAM.
- No external SDRAM/PSRAM required.

---

## 2. Real-Time Performance & CPU Budget

Measurements taken using `dimension5_bench` across $20\text{ seconds}$ ($960,000\text{ samples}$) of audio per run on desktop host systems, alongside analytical theoretical projections for the ARM Cortex-M33 architecture.

### 2.1 Measured Desktop Host Performance & BBD Overhead (48 kHz, Block = 32 frames)

| Configuration | Host Time (ns/sample) | Host CPU % (1 Core) | Overhead vs Baseline | Theoretical M33 Projection* |
| :--- | :---: | :---: | :---: | :---: |
| **BBD Character = 0% (Neutral)** | **473.7 ns** | **2.27%** | **+0.0% (Bit-exact bypass)** | ~0.8% (~40 cycles) |
| BBD Character = 25% (Discreet) | 576.6 ns | 2.77% | +21.7% | ~0.9% (~46 cycles) |
| BBD Character = 50% (Musical) | 577.6 ns | 2.77% | +21.9% | ~0.9% (~46 cycles) |
| BBD Character = 100% (Full) | 563.9 ns | 2.71% | +19.0% | ~0.9% (~46 cycles) |

> **\*Note on Theoretical M33 Projections:**
> Figures labeled "Theoretical M33 Projection" are analytical estimates based on instruction cycle counts (~40 to 46 cycles/sample at 250 MHz), NOT physical on-target measurements. Physical verification on the STM32H562 requires measuring on target hardware via the DWT cycle counter or GPIO toggling (see section 4.1).

---

### 2.2 Host Sample Rate Scaling (BBD Character = 50%, Block = 32 frames)

| Sample Rate | Host Time (ns/sample) | Sample Period | Host CPU % | Theoretical M33 Projection* |
| :---: | :---: | :---: | :---: | :---: |
| **44,100 Hz** | 557.6 ns | 22,676 ns | 2.46% | ~0.8% |
| **48,000 Hz** | 575.1 ns | 20,833 ns | 2.76% | ~0.9% |
| **96,000 Hz** | 606.8 ns | 10,417 ns | 5.83% | ~1.8% |

---

### 2.3 Host Block Size Scaling (48 kHz, BBD Character = 50%)

| Block Size | Host Time (ns/sample) | Host CPU % | Throughput |
| :---: | :---: | :---: | :---: |
| **16 frames** | 587.4 ns | 2.82% | 1.70M samples/sec |
| **32 frames** | 585.0 ns | 2.81% | 1.71M samples/sec |
| **64 frames** | 592.5 ns | 2.84% | 1.69M samples/sec |

---

## 3. DSP Hot Path Design Guarantees

To ensure rock-solid real-time performance on microcontrollers:
1. **Zero per-sample transcendentals**: No `sinf()`, `cosf()`, `powf()`, or `expf()` in the audio loop.
2. **Zero per-sample division**: The clock frequency $f_{\text{clock}} = \frac{N_{\text{stages}}}{2 \cdot \tau}$ is computed using a 3rd-order Taylor series expansion around the base delay, precalculated at the block rate:
   $$\frac{1}{1 \pm x} \approx 1 \mp x + x^2 \mp x^3$$
   Max approximation error is $< 0.18\%$, entirely eliminating division instructions from the inner loop.
3. **No Dynamic Branching**: Delay wraparound uses bitwise masking (`& DIMENSION_DELAY_MASK`) with power-of-two buffer size ($2048$).
4. **Denormal / Underflow Immunity**: Small anti-denormal offsets or clamping prevent FPU denormal stall penalties.

---

## 4. Running the Benchmark & Target Hardware Measurement

### 4.1 Desktop Host Benchmark Execution

Build and execute the benchmark tool locally:

```bash
# Configure and build
cmake -S . -B build -G Ninja
cmake --build build --target dimension5_bench

# Run benchmark
./build/dimension5_bench
```

### 4.2 Hardware Measurement on STM32H562

To verify execution cycles directly on an STM32H562 target running at 250 MHz:

1. **DWT Cycle Counter**:
   ```c
   /* Enable DWT cycle counter in debug initialization */
   CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
   DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

   /* In the audio block callback: */
   uint32_t t0 = DWT->CYCCNT;
   Dimension_ProcessBlock(&g_dimension, inL, inR, outL, outR, frames);
   uint32_t elapsedCycles = DWT->CYCCNT - t0;
   float cyclesPerSample = (float)elapsedCycles / (float)frames;
   ```

2. **GPIO Oscilloscope / Logic Analyzer Duty Cycle**:
   Set a dedicated test GPIO pin HIGH immediately before calling `Dimension_ProcessBlock()`, and set it LOW immediately after. Measure high-pulse width vs audio block period (e.g. 666.7 µs for 32 frames @ 48 kHz) to obtain real physical duty cycle.
