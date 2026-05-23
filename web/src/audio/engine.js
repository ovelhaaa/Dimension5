const loadWasmModuleFactory = async () => {
  const wasmModulePath = '/wasm/dimension_dsp.js';
  const wasmModule = await import(/* @vite-ignore */ wasmModulePath);
  return wasmModule.default;
};

export function createEngine(setStatus) {
  let module = null, ctx = null, node = null, source = null, stream = null, bypass = false;
  let inPtrL, inPtrR, outPtrL, outPtrR;
  const scriptProcessorBufferSize = 1024;
  const maxProcessFrames = scriptProcessorBufferSize;
  const f32 = () => module.HEAPF32;

  const api = {
    async loadWasm() {
      const createModule = await loadWasmModuleFactory();
      module = await createModule();
      inPtrL = module._malloc(maxProcessFrames * 4); inPtrR = module._malloc(maxProcessFrames * 4);
      outPtrL = module._malloc(maxProcessFrames * 4); outPtrR = module._malloc(maxProcessFrames * 4);
      setStatus('WASM loaded');
    },
    async startAudio() {
      if (!module) throw new Error('Load WASM first');
      if (ctx) {
        setStatus('audio already active');
        return;
      }

      const audioContext = new AudioContext();
      try {
        await audioContext.resume();
        module._DimensionWasm_Init(audioContext.sampleRate);
        stream = await navigator.mediaDevices.getUserMedia({ audio: true });
        source = audioContext.createMediaStreamSource(stream);
      } catch (err) {
        await audioContext.close();
        const message = err?.name === 'NotAllowedError'
          ? 'Error: microphone permission denied'
          : `Error starting audio: ${err?.message ?? err}`;
        setStatus(message);
        throw err;
      }

      ctx = audioContext;
      // ScriptProcessorNode is deprecated, but kept for parity with current scaffold.
      // TODO: migrate this callback to AudioWorkletProcessor for production use.
      node = ctx.createScriptProcessor(scriptProcessorBufferSize, 2, 2);
      node.onaudioprocess = (e) => {
        const inL = e.inputBuffer.getChannelData(0);
        const inR = e.inputBuffer.numberOfChannels > 1 ? e.inputBuffer.getChannelData(1) : inL;
        const outL = e.outputBuffer.getChannelData(0);
        const outR = e.outputBuffer.getChannelData(1);
        if (inL.length > maxProcessFrames) {
          throw new Error(`ScriptProcessor buffer ${inL.length} exceeds allocated WASM buffer ${maxProcessFrames}`);
        }
        f32().set(inL, inPtrL / 4); f32().set(inR, inPtrR / 4);
        module._DimensionWasm_Process(inPtrL, inPtrR, outPtrL, outPtrR, inL.length, bypass ? 1 : 0);
        outL.set(f32().subarray(outPtrL / 4, outPtrL / 4 + inL.length));
        outR.set(f32().subarray(outPtrR / 4, outPtrR / 4 + inL.length));
      };
      source.connect(node).connect(ctx.destination);
      setStatus('audio active');
    },
    setParam(id, value) { if (module) module._DimensionWasm_SetParam(id, value); },
    setMode(mode) { if (module) module._DimensionWasm_SetMode(mode); },
    toggleBypass() { bypass = !bypass; },
    isBypassed() { return bypass; }
  };
  return api;
}
