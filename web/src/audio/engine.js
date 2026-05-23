const WORKLET_PATH = '/src/audio/dimension-worklet.js';
const WASM_MODULE_PATH = '/wasm/dimension_dsp.js';

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
  const paramState = new Map();

  async function ensureAudio() {
    if (ctx && node) return;
    ctx = new AudioContext({ latencyHint: 'interactive' });
    await ctx.audioWorklet.addModule(WORKLET_PATH);
    node = new AudioWorkletNode(ctx, 'dimension-processor', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
    node.connect(ctx.destination);
    node.port.onmessage = (ev) => {
      if (ev.data?.type === 'ready') setStatus('WASM pronto no AudioWorklet');
      if (ev.data?.type === 'error') setStatus(`erro no worklet: ${ev.data.message}`);
    };
    node.port.postMessage({ type: 'init', moduleUrl: WASM_MODULE_PATH, sampleRate: ctx.sampleRate });
  }

  function applyStateToPort(port) {
    port.postMessage({ type: 'setMode', mode: currentMode });
    port.postMessage({ type: 'setBypass', value: bypass });
    for (const [name, value] of paramState.entries()) {
      port.postMessage({ type: 'setParam', paramId: PARAMS[name], value });
    }
  }

  return {
    async init() {
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
    hasFileLoaded() { return !!loadedBuffer; },
    async renderOffline() {
      if (!loadedBuffer) throw new Error('Nenhum arquivo carregado');
      const offline = new OfflineAudioContext({ numberOfChannels: 2, length: loadedBuffer.length, sampleRate: loadedBuffer.sampleRate });
      await offline.audioWorklet.addModule(WORKLET_PATH);
      const offlineNode = new AudioWorkletNode(offline, 'dimension-processor', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
      offlineNode.connect(offline.destination);
      offlineNode.port.postMessage({ type: 'init', moduleUrl: WASM_MODULE_PATH, sampleRate: offline.sampleRate });
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
