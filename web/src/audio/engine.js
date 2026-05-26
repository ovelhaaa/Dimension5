const WORKLET_URL = new URL(`./dimension-worklet.js?base=${encodeURIComponent(import.meta.env.BASE_URL || '/')}`, import.meta.url);
const WASM_JS_PATH = new URL('wasm/dimension_dsp.js', globalThis.location?.origin ? new URL(import.meta.env.BASE_URL, globalThis.location.origin) : import.meta.env.BASE_URL).toString();
const WASM_BIN_PATH = new URL('wasm/dimension_dsp.wasm', globalThis.location?.origin ? new URL(import.meta.env.BASE_URL, globalThis.location.origin) : import.meta.env.BASE_URL).toString();

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

const PARAM_NAMES = Object.keys(PARAMS);

function encodeWavBlob(left, right, sampleRate) { /* unchanged */
  const frames = left.length; const channels = 2; const bytesPerSample = 2;
  const dataSize = frames * channels * bytesPerSample;
  const buffer = new ArrayBuffer(44 + dataSize); const view = new DataView(buffer);
  const writeString = (o, s) => { for (let i = 0; i < s.length; i += 1) view.setUint8(o + i, s.charCodeAt(i)); };
  writeString(0, 'RIFF'); view.setUint32(4, 36 + dataSize, true); writeString(8, 'WAVE'); writeString(12, 'fmt ');
  view.setUint32(16, 16, true); view.setUint16(20, 1, true); view.setUint16(22, 2, true); view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * channels * bytesPerSample, true); view.setUint16(32, channels * bytesPerSample, true); view.setUint16(34, 16, true);
  writeString(36, 'data'); view.setUint32(40, dataSize, true);
  let offset = 44;
  for (let i = 0; i < frames; i += 1) {
    const l = Math.max(-1, Math.min(1, left[i])); const r = Math.max(-1, Math.min(1, right[i]));
    view.setInt16(offset, l < 0 ? l * 0x8000 : l * 0x7fff, true); offset += 2;
    view.setInt16(offset, r < 0 ? r * 0x8000 : r * 0x7fff, true); offset += 2;
  }
  return new Blob([buffer], { type: 'audio/wav' });
}

export function createEngine(setStatus) {
  let ctx = null; let node = null; let sourceNode = null; let loadedBuffer = null;
  let bypass = false; let repeat = true; let currentMode = 0; let initCounter = 0;
  const paramState = new Map();
  let wasmBytesCache = null;

  async function loadWasmBytes() {
    if (wasmBytesCache) return wasmBytesCache;
    const response = await fetch(WASM_BIN_PATH);
    if (!response.ok) throw new Error(`WASM bin indisponível (${response.status}) em ${WASM_BIN_PATH}`);
    wasmBytesCache = await response.arrayBuffer();
    return wasmBytesCache;
  }

  async function assertWasmLoaderReachable() {
    const response = await fetch(WASM_JS_PATH, { method: 'GET' });
    if (!response.ok) throw new Error(`WASM loader indisponível (${response.status}) em ${WASM_JS_PATH}`);
  }

  function applyStateToNode(targetNode) {
    targetNode.port.postMessage({ type: 'setMode', mode: currentMode });
    targetNode.port.postMessage({ type: 'setBypass', value: bypass });
    for (const [name, value] of paramState.entries()) {
      const param = targetNode.parameters.get(name);
      if (param) param.setValueAtTime(value, targetNode.context.currentTime);
      targetNode.port.postMessage({ type: 'setParam', paramId: PARAMS[name], value });
    }
  }

  function waitForReady(targetNode, requestId, wasmBytes) {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('Timeout inicializando AudioWorklet/WASM')), 12000);
      const handler = (ev) => {
        if (ev.data?.requestId !== requestId) return;
        if (ev.data?.type === 'ready') { clearTimeout(timeout); targetNode.port.onmessage = null; resolve(); }
        if (ev.data?.type === 'error') { clearTimeout(timeout); targetNode.port.onmessage = null; reject(new Error(ev.data.message)); }
      };
      targetNode.port.onmessage = handler;
      targetNode.port.postMessage({ type: 'init', sampleRate: targetNode.context.sampleRate, wasmBytes, requestId });
    });
  }

  async function ensureAudio() {
    if (!ctx) {
      ctx = new AudioContext({ latencyHint: 'interactive' });
      await ctx.audioWorklet.addModule(WORKLET_URL, { type: 'module' });
    }
    if (!node) {
      await assertWasmLoaderReachable();
      const wasmBytes = await loadWasmBytes();
      node = new AudioWorkletNode(ctx, 'dimension-processor', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
      node.connect(ctx.destination);
      const requestId = `main-${++initCounter}`;
      await waitForReady(node, requestId, wasmBytes);
      applyStateToNode(node);
      setStatus('WASM pronto no AudioWorklet');
    }
  }

  return {
    async initFromGesture() { await ensureAudio(); await ctx.resume(); },
    async loadAudioFile(file) { await ensureAudio(); loadedBuffer = await ctx.decodeAudioData(await file.arrayBuffer()); setStatus(`arquivo carregado: ${file.name}`); },
    async playLoadedFile() { if (!loadedBuffer) throw new Error('Nenhum arquivo carregado'); await ensureAudio(); await ctx.resume(); if (sourceNode) { try { sourceNode.stop(); } catch (_) {} sourceNode.disconnect(); }
      sourceNode = ctx.createBufferSource(); sourceNode.buffer = loadedBuffer; sourceNode.loop = repeat; sourceNode.connect(node); sourceNode.start(); setStatus('tocando arquivo em loop/processado'); },
    async stopPlayback() { if (sourceNode) { try { sourceNode.stop(); } catch (_) {} sourceNode.disconnect(); sourceNode = null; } setStatus('playback parado'); },
    setMode(mode) { currentMode = mode; if (node) node.port.postMessage({ type: 'setMode', mode }); },
    setParam(name, value) { paramState.set(name, value); if (node) { const p = node.parameters.get(name); if (p) p.setValueAtTime(value, ctx.currentTime); node.port.postMessage({ type: 'setParam', paramId: PARAMS[name], value }); } },
    toggleBypass() { bypass = !bypass; if (node) node.port.postMessage({ type: 'setBypass', value: bypass }); return bypass; },
    toggleRepeat() { repeat = !repeat; return repeat; },
    async renderOffline() {
      if (!loadedBuffer) throw new Error('Nenhum arquivo carregado');
      await assertWasmLoaderReachable();
      const wasmBytes = await loadWasmBytes();
      const offline = new OfflineAudioContext({ numberOfChannels: 2, length: loadedBuffer.length, sampleRate: loadedBuffer.sampleRate });
      await offline.audioWorklet.addModule(WORKLET_URL, { type: 'module' });
      const offNode = new AudioWorkletNode(offline, 'dimension-processor', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
      offNode.connect(offline.destination);
      const requestId = `offline-${++initCounter}`;
      await waitForReady(offNode, requestId, wasmBytes);
      applyStateToNode(offNode);
      const src = offline.createBufferSource(); src.buffer = loadedBuffer; src.connect(offNode); src.start(0);
      const rendered = await offline.startRendering();
      return encodeWavBlob(rendered.getChannelData(0), rendered.getChannelData(1), rendered.sampleRate);
    }
  };
}
