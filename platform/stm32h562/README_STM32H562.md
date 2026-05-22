# Integração STM32H562

Este diretório contém apenas integração de plataforma (HAL/LL/SAI/I2S/DMA) e exemplos de callback.

## Regras

- O núcleo DSP permanece em `src/` e não depende de HAL.
- Conversão de formato (int16/int24/float) fica aqui.
- Inicialização de periféricos STM32 fica aqui.

## Arquivo base

- `dimension_stm32_example.c`: exemplo mínimo de callback de áudio em blocos para chamar `Dimension_ProcessBlock`.
