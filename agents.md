# AGENTS.md

## Projeto

Este repositório implementa um simulador embarcado do efeito **Roland/Boss Dimension chorus** para **STM32H562**, com foco em áudio estéreo em tempo real, baixa latência, estabilidade numérica e eficiência em Cortex-M33 com FPU/DSP.

O objetivo não é criar um chorus genérico. O objetivo é reproduzir a assinatura psicoacústica da família Dimension:

- modulação sutil;
- imagem estéreo larga;
- centro preservado;
- graves estáveis;
- ausência de warble/vibrato exagerado;
- atrasos modulados em antifase;
- matriz estéreo com inversão cruzada;
- coloração analógica estilo BBD-lite;
- compander leve inspirado no NE570/571.

O alvo inicial é uma implementação **BBD-lite**, não um modelo físico completo de BBD.

---

## Plataforma alvo

Alvo primário:

```text
MCU: STM32H562
Core: Arm Cortex-M33
Formato interno: float32
Sample rate primário: 48 kHz
Áudio: estéreo
Bloco preferencial: 32 frames
Blocos aceitos: 16, 32 ou 64 frames
Codec:externo via SAI/I2S + DMA circular/double-buffer
O núcleo DSP deve ser independente de HAL. A integração com STM32 HAL, LL, SAI, I2S, DMA ou codec deve ficar em arquivos separados.

## Regras obrigatórias
Agentes devem obedecer rigorosamente:
``` Plain text
Não usar malloc/free/new/delete no processamento de áudio.
Não usar STL no hot path.
Não usar exceções C++ no núcleo DSP.
Não usar RTTI.
Não usar printf/logs dentro do callback de áudio.
Não usar sinf/cosf/tanhf/powf por amostra.
Não usar divisão por amostra quando houver alternativa simples.
Não recalcular coeficientes de filtros por amostra.
Não colocar buffers grandes na stack.
Não acessar memória externa para delay lines.
Não introduzir dependência obrigatória de HAL no núcleo DSP.
Não alterar parâmetros abruptamente.
Não gerar NaN/Inf com entrada entre -2.0 e +2.0.
Não implementar modelo físico BBD completo antes do BBD-lite estar validado.
Preferir C99 ou C++ sem features pesadas. Para máxima portabilidade embarcada, o núcleo DSP deve compilar como C sempre que possível. ```
```

## Estrutura esperada do repositório
``` Plain text
/
├── AGENTS.md
├── README.md
├── src/
│   ├── dimension_dsp.c
│   ├── dimension_dsp.h
│   ├── dimension_config.h
│   └── dimension_tables.h          # opcional
├── platform/
│   └── stm32h562/
│       ├── dimension_stm32_example.c
│       ├── audio_callback_example.c
│       └── README_STM32H562.md
├── tests/
│   ├── test_dimension_core.c
│   ├── test_vectors/
│   └── offline_analysis.py         # opcional
├── bench/
│   ├── dimension_bench.c
│   └── README_BENCH.md
└── docs/
    ├── architecture.md
    ├── tuning.md
    └── validation.md
```
Arquivos HAL/CubeMX gerados não devem contaminar o núcleo DSP.

## API pública obrigatória

O núcleo deve expor uma API semelhante a:
``` C
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIMENSION_MAX_BLOCK_SIZE 64
#define DIMENSION_SAMPLE_RATE_DEFAULT 48000.0f
#define DIMENSION_DELAY_MAX_MS 32.0f

typedef enum DimensionMode {
    DIMENSION_MODE_I = 0,
    DIMENSION_MODE_II,
    DIMENSION_MODE_III,
    DIMENSION_MODE_IV,
    DIMENSION_MODE_CUSTOM
} DimensionMode;

typedef enum DimensionQuality {
    DIMENSION_QUALITY_PERCEPTUAL = 0,
    DIMENSION_QUALITY_ANALOG_LITE,
    DIMENSION_QUALITY_BBD_LITE
} DimensionQuality;

typedef struct DimensionParams {
    float sampleRate;

    DimensionMode mode;
    DimensionQuality quality;

    float inputGain;
    float outputGain;

    float dryGain;
    float wetDirectGain;
    float wetCrossGain;

    float baseDelayMs;
    float depthMs;
    float rateHz;

    float hpfHz;
    float lpfHz;

    float analogAmount;
    float companderAmount;
    float noiseAmount;

    float width;
} DimensionParams;

typedef struct DimensionDSP DimensionDSP;

void Dimension_Init(DimensionDSP* d, float sampleRate);
void Dimension_Reset(DimensionDSP* d);
void Dimension_SetMode(DimensionDSP* d, DimensionMode mode);
void Dimension_SetParams(DimensionDSP* d, const DimensionParams* p);
void Dimension_GetParams(const DimensionDSP* d, DimensionParams* p);

void Dimension_ProcessBlock(
    DimensionDSP* d,
    const float* inL,
    const float* inR,
    float* outL,
    float* outR,
    uint32_t n
);

#ifdef __cplusplus
}
#endif
```

A estrutura DimensionDSP pode ser opaca, mas para firmware embarcado é preferível disponibilizar tamanho estático conhecido em tempo de compilação, evitando alocação dinâmica.

## Configuração padrão
Arquivo recomendado: src/dimension_config.h.
```
C
#pragma once

#define DIMENSION_TARGET_STM32H562 1

#define DIMENSION_SAMPLE_RATE_DEFAULT 48000.0f
#define DIMENSION_MAX_BLOCK_SIZE 64

#define DIMENSION_DELAY_SIZE 2048
#define DIMENSION_DELAY_MASK (DIMENSION_DELAY_SIZE - 1)

#define DIMENSION_USE_FLOAT32 1
#define DIMENSION_USE_CMSIS_DSP 0

#define DIMENSION_ENABLE_NOISE 0
#define DIMENSION_ENABLE_CLOCK_BLEED 0
#define DIMENSION_ENABLE_SAFETY_LIMITER 1

#define DIMENSION_INTERP_LINEAR  0
#define DIMENSION_INTERP_HERMITE 1
#define DIMENSION_INTERP_LAGRANGE3 2

#define DIMENSION_INTERP_MODE DIMENSION_INTERP_HERMITE

#define DIMENSION_QUALITY_DEFAULT DIMENSION_QUALITY_BBD_LITE

#if defined(__GNUC__)
#define DIMENSION_ALIGN_32 __attribute__((aligned(32)))
#else
#define DIMENSION_ALIGN_32
#endif
```
Para 48 kHz e atraso máximo de 32 ms:
``` Plain text
48000 * 0.032 = 1536 amostras
Use DIMENSION_DELAY_SIZE = 2048 por linha. Com duas linhas float32, o custo-base é aproximadamente 16 kB.
Arquitetura DSP obrigatória
O fluxo de áudio deve seguir:
Plain text
Input L/R
  -> input trim/headroom
  -> split dry/wet
  -> HPF 1ª ordem no wet path
  -> compressor simplificado 2:1
  -> pré-filtragem BBD
  -> duas delay lines fracionárias moduladas em antifase
  -> interpolação Hermite cúbica
  -> LPF de reconstrução estilo BBD
  -> saturação suave / perda de carga
  -> expander simplificado 1:2
  -> matriz Dimension com inversão cruzada
  -> output gain / safety limiter leve
```
# Output L/R
A matriz Dimension deve ser:

``` C
outL = gDry * inL + gWet1 * wetL - gWet2 * wetR;
outR = gDry * inR + gWet1 * wetR - gWet2 * wetL;
``` 
Valores iniciais:
``` C
gDry  = 0.83f;
gWet1 = 0.50f;
gWet2 = 0.35f;
```
Esses ganhos devem ser parametrizáveis.

## LFO
O LFO deve ser triangular ou trapezoidal, sem função trigonométrica por amostra.

# Regras:
``` Plain text
Fase normalizada em [0, 1).
Rate em Hz.
Atualização por amostra.
Saída em [-1, +1].
Delay esquerdo e direito em antifase exata.
Smoothing de rate, depth e baseDelay.
Relação obrigatória:
```
``` C
delayL_ms = baseDelayMs + depthMs * lfo;
delayR_ms = baseDelayMs - depthMs * lfo;
```
A soma deve permanecer estável:

```Plain text
delayL_ms + delayR_ms ≈ 2 * baseDelayMs
```

## Modos Dimension
Implementar os quatro modos iniciais:
``` Plain text
# Mode I:
  rateHz          = 0.25
  baseDelayMs     = 7.0
  depthMs         = 0.9
  hpfHz           = 120
  lpfHz           = 8000
  wetScale        = 0.80
  analogAmount    = 0.35
  companderAmount = 0.35

# Mode II:
  rateHz          = 0.25
  baseDelayMs     = 8.5
  depthMs         = 1.4
  hpfHz           = 120
  lpfHz           = 7500
  wetScale        = 0.95
  analogAmount    = 0.45
  companderAmount = 0.45

# Mode III:
  rateHz          = 0.50
  baseDelayMs     = 10.0
  depthMs         = 1.8
  hpfHz           = 130
  lpfHz           = 7000
  wetScale        = 1.00
  analogAmount    = 0.50
  companderAmount = 0.50

# Mode IV:
  rateHz          = 0.50
  baseDelayMs     = 11.5
  depthMs         = 2.4
  hpfHz           = 140
  lpfHz           = 6500
  wetScale        = 1.10
  analogAmount    = 0.60
  companderAmount = 0.60

```
wetScale deve afetar wetDirectGain e wetCrossGain, não dryGain.
Troca de modo deve ser suavizada em aproximadamente 20–50 ms.

## Delay fracionário
Implementar duas linhas de delay independentes.

# Requisitos:
``` Plain text
Buffer circular com tamanho potência de 2.
Write index com máscara, não módulo.
Máximo delay >= 32 ms.
Leitura fracionária segura no wraparound.
Hermite cúbica por padrão.
Linear opcional para benchmark.
Sem leitura fora do buffer.
```

# Interpolador Hermite aceitável:
``` C
static inline float interp_hermite4(float xm1, float x0, float x1, float x2, float t)
{
    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f*x0 + 2.0f*x1 - 0.5f*x2;
    const float c3 = 0.5f*(x2 - xm1) + 1.5f*(x0 - x1);
    return ((c3*t + c2)*t + c1)*t + c0;
}
`` `
## Filtros
Implementar:
Plain text
HPF 1ª ordem no wet input.
LPF 2ª ordem ou dois LPFs de 1ª ordem no wet output.
Estados separados por canal.
Coeficientes recalculados apenas quando parâmetros mudam.
Proteção contra denormais.
Reset limpo.
Critérios:
Plain text
HPF deve reduzir modulação de graves.
LPF deve remover aspereza da interpolação.
Filtros devem ser estáveis para 44.1, 48 e 96 kHz.
Compander
Implementar compander simplificado, inspirado no NE570/571.
Compressor antes do delay:
Plain text
Detector por valor absoluto suavizado.
Ataque/release.
Ratio aproximado 2:1.
Threshold interno seguro.
Amount 0–1.
Expander depois do delay:
Plain text
Expansão complementar aproximada.
Floor mínimo de envelope.
Sem pumping exagerado.
Sem ganho infinito em silêncio.
Critérios:
Plain text
companderAmount = 0: caminho quase linear.
companderAmount = 1: compressão/expansão audível, mas musical.
Entrada silenciosa não explode ganho.
Transientes não geram clicks.
BBD-lite
Implementar coloração analógica leve, controlada por analogAmount.
Componentes permitidos:
Plain text
Soft clipping rápido.
Perda de carga dependente do delay.
LPF levemente dependente do modo.
Ruído opcional muito baixo.
Clock bleed opcional, desativado por padrão.
Não implementar nesta fase:
Plain text
BBD de clock variável físico completo.
Reamostragem complexa em taxa variável.
Aliasing dinâmico explícito.
Oversampling global.
Saturador preferencial no hot path:
C
static inline float softclip_cubic(float x)
{
    if (x > 1.0f)  return  0.6666667f;
    if (x < -1.0f) return -0.6666667f;
    return x - (x * x * x) * 0.3333333f;
}
Integração STM32
O callback STM32 deve ficar fora do núcleo DSP.
Exemplo de padrão aceitável:
C
void Audio_ProcessHalfBuffer(int16_t* rx, int16_t* tx, uint32_t frames)
{
    static float inL[DIMENSION_MAX_BLOCK_SIZE];
    static float inR[DIMENSION_MAX_BLOCK_SIZE];
    static float outL[DIMENSION_MAX_BLOCK_SIZE];
    static float outR[DIMENSION_MAX_BLOCK_SIZE];

    while (frames > 0) {
        uint32_t n = frames > DIMENSION_MAX_BLOCK_SIZE
                   ? DIMENSION_MAX_BLOCK_SIZE
                   : frames;

        for (uint32_t i = 0; i < n; ++i) {
            inL[i] = rx[2*i + 0] * (1.0f / 32768.0f);
            inR[i] = rx[2*i + 1] * (1.0f / 32768.0f);
        }

        Dimension_ProcessBlock(&g_dimension, inL, inR, outL, outR, n);

        for (uint32_t i = 0; i < n; ++i) {
            float l = outL[i];
            float r = outR[i];

            if (l > 0.999f) l = 0.999f;
            if (l < -1.0f) l = -1.0f;
            if (r > 0.999f) r = 0.999f;
            if (r < -1.0f) r = -1.0f;

            tx[2*i + 0] = (int16_t)(l * 32767.0f);
            tx[2*i + 1] = (int16_t)(r * 32767.0f);
        }

        rx += 2*n;
        tx += 2*n;
        frames -= n;
    }
}
Adapte para int24, int32 ou float se o codec exigir, mantendo o núcleo em float32.
Performance
Meta no STM32H562:
Plain text
Sample rate: 48 kHz
Bloco preferencial: 32 frames
CPU médio: abaixo de 50%
CPU pico: abaixo de 70%
Sem underrun por 10 minutos
Instrumentação recomendada:
Plain text
GPIO high antes do processamento DSP.
GPIO low depois do processamento DSP.
Medir duty cycle em osciloscópio/analisador lógico.
Opcionalmente usar DWT->CYCCNT, se disponível.
Qualquer benchmark deve ser desativável por macro e não deve afetar o hot path quando desligado.
Flags de compilação recomendadas
Plain text
-O3
-fsingle-precision-constant
-ffunction-sections
-fdata-sections
-mcpu=cortex-m33
-mfloat-abi=hard
Adicionar FPU conforme o toolchain/dispositivo.
Não usar -ffast-math até os testes de estabilidade confirmarem ausência de NaN/Inf, instabilidade de filtros ou comportamento indesejado do compander.
Testes obrigatórios
Testes offline
Implementar ou manter testes para:
Plain text
Entrada zero por período longo: sem NaN/Inf e DC baixo.
Impulso com delay fixo: atraso medido correto.
Senoide 1 kHz: sem distorção excessiva com analogAmount = 0.
Senoide 100 Hz: graves preservados no centro.
Ruído rosa: estabilidade.
Entrada ±2.0: saturação segura.
Troca de modo a cada 1 s: sem clicks.
Variação de sample rate: 44.1, 48 e 96 kHz.
Testes no hardware
Validar no STM32H562:
Plain text
Callback medido por GPIO ou DWT.
Blocos de 16, 32 e 64 frames.
Todos os modos I–IV.
Bypass/effect toggle.
Entrada mono para saída estéreo.
Entrada estéreo real.
Rodar 10 minutos sem underrun.
Critérios de aceitação
Uma alteração só deve ser considerada correta quando:
Plain text
Compila sem warnings relevantes.
Não quebra a API pública.
Processa estéreo 48 kHz.
Não usa alocação dinâmica no callback.
Não gera NaN/Inf.
Não gera clicks em troca de modo.
Preserva graves e imagem central.
Abre a imagem estéreo de forma perceptível.
Não soa como vibrato/chorus exagerado.
Mantém núcleo DSP separado da HAL.
Inclui ou atualiza testes quando altera DSP.
Orientação para agentes
Ao modificar o repositório:
Leia este AGENTS.md antes de alterar código.
Preserve determinismo de tempo real acima de conveniência.
Prefira mudanças pequenas e testáveis.
Não adicione dependências grandes.
Não mova código HAL para dentro do núcleo DSP.
Não otimize prematuramente sacrificando clareza, mas mantenha o hot path enxuto.
Qualquer alteração em filtros, compander, LFO ou matriz deve vir acompanhada de teste ou justificativa técnica.
Documente limitações conhecidas em vez de escondê-las.
O alvo correto desta fase é um Dimension BBD-lite musical, eficiente e robusto no STM32H562.
