const loadWasmModuleFactory = async () => {
  const wasmModulePath = '/wasm/dimension_dsp.js';
  const wasmModule = await import(/* @vite-ignore */ wasmModulePath);
  return wasmModule.default;
};

export function createEngine(setStatus) {
  let module = null;
  let ctx = null;
  let node = null;
  let sourceNode = null;
  let micStream = null;
  let bypass = false;
  let killDry = false;
  let repeat = false;

  let inPtrL; let inPtrR; let outPtrL; let outPtrR;
  const scriptProcessorBufferSize = 1024;
  const maxProcessFrames = scriptProcessorBufferSize;
  const f32 = () => module.HEAPF32;

  let loadedBuffer = null;
  let mediaDest = null;
  let recorder = null;
  let recordedChunks = [];
  let lastRenderedBlob = null;
  let isPlayingFile = false;

  const teardownInput = ({ stopMic = true, stopNode = true } = {}) => {
    if (sourceNode) {
      sourceNode.onended = null;
      if (stopNode && typeof sourceNode.stop === 'function') {
        try { sourceNode.stop(); } catch (_) {}
      }
      sourceNode.disconnect();
      sourceNode = null;
    }

    if (stopMic && micStream) {
      micStream.getTracks().forEach((track) => track.stop());
      micStream = null;
    }
  };

  const wireProcessor = () => {
    node = ctx.createScriptProcessor(scriptProcessorBufferSize, 2, 2);
    node.onaudioprocess = (e) => {
      const inL = e.inputBuffer.getChannelData(0);
      const inR = e.inputBuffer.numberOfChannels > 1 ? e.inputBuffer.getChannelData(1) : inL;
      const outL = e.outputBuffer.getChannelData(0);
      const outR = e.outputBuffer.getChannelData(1);

      if (inL.length > maxProcessFrames) {
        console.error(`ScriptProcessor buffer ${inL.length} exceeds allocated WASM buffer ${maxProcessFrames}`);
        return;
      }

      f32().set(inL, inPtrL / 4);
      f32().set(inR, inPtrR / 4);
      module._DimensionWasm_Process(inPtrL, inPtrR, outPtrL, outPtrR, inL.length, bypass ? 1 : 0);

      const wetL = f32().subarray(outPtrL / 4, outPtrL / 4 + inL.length);
      const wetR = f32().subarray(outPtrR / 4, outPtrR / 4 + inL.length);

      outL.set(wetL);
      outR.set(wetR);

      if (killDry) {
        // Kill Dry keeps only processed output (wet) and removes direct path,
        // which is already represented by the processed output signal here.
      }
    };

    node.connect(ctx.destination);
    mediaDest = ctx.createMediaStreamDestination();
    node.connect(mediaDest);
  };

  const startRecorder = () => {
    if (!mediaDest) return;
    const sessionChunks = [];
    const sessionRecorder = new MediaRecorder(mediaDest.stream);
    sessionRecorder.ondataavailable = (ev) => {
      if (ev.data.size > 0) sessionChunks.push(ev.data);
    };
    sessionRecorder.onstop = () => {
      lastRenderedBlob = new Blob(sessionChunks, { type: sessionRecorder.mimeType || 'audio/webm' });
    };
    recorder = sessionRecorder;
    recordedChunks = sessionChunks;
    sessionRecorder.start();
  };

  const stopRecorder = () => {
    if (recorder && recorder.state !== 'inactive') recorder.stop();
  };

  const api = {
    async loadWasm() {
      const createModule = await loadWasmModuleFactory();
      module = await createModule();
      inPtrL = module._malloc(maxProcessFrames * 4);
      inPtrR = module._malloc(maxProcessFrames * 4);
      outPtrL = module._malloc(maxProcessFrames * 4);
      outPtrR = module._malloc(maxProcessFrames * 4);
      setStatus('WASM loaded');
    },

    async initAudioGraph() {
      if (!module) throw new Error('Load WASM first');
      if (ctx) return;
      ctx = new AudioContext();
      await ctx.resume();
      module._DimensionWasm_Init(ctx.sampleRate);
      wireProcessor();
    },

    async startMicAudio() {
      await api.initAudioGraph();
      teardownInput({ stopMic: true, stopNode: true });
      stopRecorder();
      isPlayingFile = false;
      micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
      sourceNode = ctx.createMediaStreamSource(micStream);
      sourceNode.connect(node);
      setStatus('audio active: microphone');
    },

    async loadAudioFile(file) {
      await api.initAudioGraph();
      const arrayBuf = await file.arrayBuffer();
      loadedBuffer = await ctx.decodeAudioData(arrayBuf);
      setStatus(`file loaded: ${file.name}`);
    },

    async playLoadedFile(onEnded) {
      if (!loadedBuffer) throw new Error('No audio file loaded');
      await api.initAudioGraph();
      teardownInput({ stopMic: true, stopNode: true });
      stopRecorder();

      sourceNode = ctx.createBufferSource();
      sourceNode.buffer = loadedBuffer;
      sourceNode.loop = repeat;
      sourceNode.connect(node);
      isPlayingFile = true;
      startRecorder();
      sourceNode.onended = () => {
        isPlayingFile = false;
        stopRecorder();
        sourceNode = null;
        if (onEnded) onEnded();
      };
      sourceNode.start();
      setStatus('audio active: file playback');
    },

    stopPlayback() {
      teardownInput({ stopMic: false, stopNode: true });
      isPlayingFile = false;
      stopRecorder();
      setStatus('playback stopped');
    },

    isPlayingFile() { return isPlayingFile; },
    setParam(id, value) { if (module) module._DimensionWasm_SetParam(id, value); },
    setMode(mode) { if (module) module._DimensionWasm_SetMode(mode); },
    toggleBypass() { bypass = !bypass; return bypass; },
    isBypassed() { return bypass; },
    toggleKillDry() { killDry = !killDry; return killDry; },
    isKillDry() { return killDry; },
    toggleRepeat() { repeat = !repeat; return repeat; },
    isRepeat() { return repeat; },
    getDownloadBlob() { return lastRenderedBlob; },
    hasRecordingInProgress() { return recorder && recorder.state === 'recording'; }
  };

  return api;
}
