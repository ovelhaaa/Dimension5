const WORKLET_URL = new URL('./dimension-worklet.js', import.meta.url);
const WASM_MODULE_PATH = new URL('wasm/dimension_dsp.js', globalThis.location?.origin ? new URL(import.meta.env.BASE_URL, globalThis.location.origin) : import.meta.env.BASE_URL).toString();

const PARAMS = {
  inputGain: 0,
  outputGain: 1,
  dryGain: 2,
  wetDirectGain: 3,
  wetCrossGain: 4,
  baseDelayMs: 5,
  depthMs: 6,
  rateHz: 7,
  hpfHz: 8,
  lpfHz: 9,
  analogAmount: 10,
  companderAmount: 11,
  width: 12
};

function encodeWavBlob(left, right, sampleRate) {
  const frames = left.length;
  const channels = 2;
  const bytesPerSample = 2;
  const dataSize = frames * channels * bytesPerSample;
  const buffer = new ArrayBuffer(44 + dataSize);
  const view = new DataView(buffer);
  const writeString = (offset, str) => { for (let i = 0; i < str.length; i += 1) view.setUint8(offset + i, str.charCodeAt(i)); };

  writeString(0, 'RIFF');
  view.setUint32(4, 36 + dataSize, true);
  writeString(8, 'WAVE');
  writeString(12, 'fmt ');
  view.setUint32(16, 16, true); view.setUint16(20, 1, true); view.setUint16(22, 2, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * channels * bytesPerSample, true);
  view.setUint16(32, channels * bytesPerSample, true); view.setUint16(34, 16, true);
  writeString(36, 'data'); view.setUint32(40, dataSize, true);

  let offset = 44;
  for (let i = 0; i < frames; i += 1) {
    const l = Math.max(-1, Math.min(1, left[i]));
    const r = Math.max(-1, Math.min(1, right[i]));
    view.setInt16(offset, l < 0 ? l * 0x8000 : l * 0x7fff, true); offset += 2;
    view.setInt16(offset, r < 0 ? r * 0x8000 : r * 0x7fff, true); offset += 2;
  }
  return new Blob([buffer], { type: 'audio/wav' });
}

export function createEngine(setStatus) {
  let ctx = null;
  let node = null;
  let sourceNode = null;
  let loadedBuffer = null;
  let bypass = false;
  let repeat = true;
  let currentMode = 0;
  let initCounter = 0;
  const pendingReady = new Map();
  const paramState = new Map();

  const WASM_CHECK_TIMEOUT_MS = 3000;
  let wasmCheckPromise = null;

  async function fetchWithTimeout(url, options, timeoutMs) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), timeoutMs);
    try {
      return await fetch(url, { ...options, signal: controller.signal });
    } finally {
      clearTimeout(timeout);
    }
  }

  async function assertWasmModuleReachable() {
    if (wasmCheckPromise) return wasmCheckPromise;

    wasmCheckPromise = (async () => {
      try {
        const headResponse = await fetchWithTimeout(WASM_MODULE_PATH, { method: 'HEAD' }, WASM_CHECK_TIMEOUT_MS);
        if (headResponse.ok) return;
        if (headResponse.status !== 405 && headResponse.status !== 501) {
          throw new Error(`WASM indisponível em ${WASM_MODULE_PATH} (HEAD ${headResponse.status})`);
        }
      } catch (err) {
        if (err?.name !== 'AbortError') {
          console.warn('HEAD check falhou para WASM, tentando GET...', err);
        }
      }

      try {
        const getResponse = await fetchWithTimeout(WASM_MODULE_PATH, { method: 'GET' }, WASM_CHECK_TIMEOUT_MS);
        if (!getResponse.ok) {
          throw new Error(`WASM indisponível em ${WASM_MODULE_PATH} (GET ${getResponse.status})`);
        }
      } catch (err) {
        wasmCheckPromise = null;
        if (err?.name === 'AbortError') {
          throw new Error(`Timeout ao verificar WASM em ${WASM_MODULE_PATH}`);
        }
        throw new Error(err?.message || `Falha ao verificar WASM em ${WASM_MODULE_PATH}`);
      }
    })();

    return wasmCheckPromise;
  }

  function waitForReady(port, requestId) {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        pendingReady.delete(requestId);
        reject(new Error(`Timeout inicializando AudioWorklet/WASM (${WASM_MODULE_PATH})`));
      }, 10000);
      pendingReady.set(requestId, { resolve, reject, timeout });
      port.postMessage({ type: 'init', sampleRate: (ctx?.sampleRate || 48000), requestId });
    });
  }

  function handlePortMessage(ev) {
    if (ev.data?.type === 'ready') {
      const requestId = ev.data.requestId;
      if (pendingReady.has(requestId)) {
        const pending = pendingReady.get(requestId);
        clearTimeout(pending.timeout);
        pendingReady.delete(requestId);
        pending.resolve();
      }
      setStatus('WASM pronto no AudioWorklet');
    }
    if (ev.data?.type === 'error') {
      const requestId = ev.data.requestId;
      if (pendingReady.has(requestId)) {
        const pending = pendingReady.get(requestId);
        clearTimeout(pending.timeout);
        pendingReady.delete(requestId);
        pending.reject(new Error(ev.data.message));
      }
      setStatus(`erro no worklet: ${ev.data.message}`);
    }
  }

  function applyStateToPort(port) {
    port.postMessage({ type: 'setMode', mode: currentMode });
    port.postMessage({ type: 'setBypass', value: bypass });
    for (const [name, value] of paramState.entries()) {
      port.postMessage({ type: 'setParam', paramId: PARAMS[name], value });
    }
  }

  async function ensureAudio() {
    if (!ctx) {
      ctx = new AudioContext({ latencyHint: 'interactive' });
      await ctx.audioWorklet.addModule(WORKLET_URL, { type: 'module' });
    }
    if (!node) {
      await assertWasmModuleReachable();
      if (node) return;
      node = new AudioWorkletNode(ctx, 'dimension-processor', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
      node.connect(ctx.destination);
      node.port.onmessage = handlePortMessage;
      const requestId = `main-${++initCounter}`;
      await waitForReady(node.port, requestId);
      applyStateToPort(node.port);
    }
  }

  return {
    async initFromGesture() {
      await ensureAudio();
      await ctx.resume();
    },
    async loadAudioFile(file) {
      await ensureAudio();
      const arrayBuf = await file.arrayBuffer();
      loadedBuffer = await ctx.decodeAudioData(arrayBuf);
      setStatus(`arquivo carregado: ${file.name}`);
    },
    async playLoadedFile() {
      if (!loadedBuffer) throw new Error('Nenhum arquivo carregado');
      await ensureAudio();
      await ctx.resume();
      if (sourceNode) { try { sourceNode.stop(); } catch (_) {} sourceNode.disconnect(); }
      sourceNode = ctx.createBufferSource();
      sourceNode.buffer = loadedBuffer;
      sourceNode.loop = repeat;
      sourceNode.connect(node);
      sourceNode.start();
      setStatus('tocando arquivo em loop/processado');
    },
    async stopPlayback() {
      if (sourceNode) {
        try { sourceNode.stop(); } catch (_) {}
        sourceNode.disconnect();
        sourceNode = null;
      }
      setStatus('playback parado');
    },
    setMode(mode) {
      currentMode = mode;
      if (node) node.port.postMessage({ type: 'setMode', mode });
    },
    setParam(name, value) {
      paramState.set(name, value);
      if (node) node.port.postMessage({ type: 'setParam', paramId: PARAMS[name], value });
    },
    toggleBypass() {
      bypass = !bypass;
      if (node) node.port.postMessage({ type: 'setBypass', value: bypass });
      return bypass;
    },
    toggleRepeat() { repeat = !repeat; return repeat; },
    async renderOffline() {
      if (!loadedBuffer) throw new Error('Nenhum arquivo carregado');
      await assertWasmModuleReachable();
      const offline = new OfflineAudioContext({ numberOfChannels: 2, length: loadedBuffer.length, sampleRate: loadedBuffer.sampleRate });
      await offline.audioWorklet.addModule(WORKLET_URL, { type: 'module' });
      const offlineNode = new AudioWorkletNode(offline, 'dimension-processor', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
      offlineNode.connect(offline.destination);

      const offlineRequestId = `offline-${++initCounter}`;
      await new Promise((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error('Timeout inicializando worklet offline')), 10000);
        offlineNode.port.onmessage = (ev) => {
          if (ev.data?.type === 'ready' && ev.data?.requestId === offlineRequestId) {
            clearTimeout(timeout);
            resolve();
          } else if (ev.data?.type === 'error' && ev.data?.requestId === offlineRequestId) {
            clearTimeout(timeout);
            reject(new Error(ev.data.message));
          }
        };
        offlineNode.port.postMessage({ type: 'init', sampleRate: offline.sampleRate, requestId: offlineRequestId });
      });

      applyStateToPort(offlineNode.port);

      const src = offline.createBufferSource();
      src.buffer = loadedBuffer;
      src.connect(offlineNode);
      src.start(0);
      const rendered = await offline.startRendering();
      return encodeWavBlob(rendered.getChannelData(0), rendered.getChannelData(1), rendered.sampleRate);
    }
  };
}
