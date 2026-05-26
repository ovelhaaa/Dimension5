# Dimension5

Implementação DSP do efeito Dimension em C para STM32H562, com versão web/WASM para testes em navegador.

## Build nativo de testes

```bash
gcc -O3 -std=c99 -Isrc tests/test_dimension_core.c src/dimension_dsp.c -lm -o test_dimension_core
./test_dimension_core
```

## Versão web/WASM

### Dependências

- Node.js 18+
- npm
- Emscripten (`emcc`) no PATH

### Fluxo

```bash
cd web
npm install
npm run build:wasm
npm run dev
```

### Build de produção

```bash
cd web
npm run build:wasm
npm run build
```

### Arquitetura web

- `src/dimension_dsp.c/.h`: núcleo DSP original (reutilizado sem duplicar lógica em JS)
- `src/dimension_wasm_bridge.c`: API C exportada para o navegador
- `web/scripts/build-wasm.sh`: compilação Emscripten para `web/public/wasm`
- `web/src/audio/engine.js`: integração WebAudio + chamada WASM
- `web/src/main.js`: UI e mapeamento de parâmetros

### Limitações conhecidas

- Neste ambiente CI/terminal não foi possível validar captura real de microfone/latência perceptiva.
- A integração atual usa `ScriptProcessorNode` para máxima compatibilidade; migração para `AudioWorklet` é recomendada como próximo passo.
