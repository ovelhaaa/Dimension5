# Integração STM32H562

Este diretório contém apenas integração de plataforma (HAL/LL/SAI/I2S/DMA) e exemplos de callback.

## Regras

- O núcleo DSP permanece em `src/` e não depende de HAL.
- Conversão de formato (int16/int24/float) fica aqui.
- Inicialização de periféricos STM32 fica aqui.

## Arquivo base

- `dimension_stm32_example.c`: exemplo mínimo de callback de áudio em blocos para chamar `Dimension_ProcessBlock`.

## Realtime integration checklist

- Keep HAL, LL, SAI/I2S, DMA, and codec drivers outside `src/dimension_dsp.c`.
- Use DMA circular or double-buffer mode and call `Dimension_ProcessBlock` from the half/full-buffer handoff, not from generic UI code.
- Maintain cache coherency for DMA buffers: align buffers as required by the MPU/cache configuration, clean before TX DMA reads, and invalidate after RX DMA writes when D-cache is enabled.
- Prefer 32-byte alignment for DMA-facing buffers on cached systems; the DSP state already uses `DIMENSION_ALIGN_32` for delay lines when built with GCC-compatible compilers.
- Enable the Cortex-M33 FPU in the startup/toolchain configuration and compile the platform layer with hard-float flags appropriate for STM32H562.
- Convert codec samples at the platform boundary. The DSP core expects planar float32 buffers and does not know about int16/int24/int32 wire formats.
- Recommended processing chunk: 32 stereo frames. Split larger DMA halves into chunks no larger than `DIMENSION_MAX_BLOCK_SIZE`.
- Instrument with GPIO or DWT cycle counter around the DSP call when validating underrun margin on hardware.
