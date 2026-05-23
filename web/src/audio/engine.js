import createModule from '/wasm/dimension_dsp.js';

export function createEngine(setStatus) {
  let module = null, ctx = null, node = null, bypass = false;
  let inPtrL, inPtrR, outPtrL, outPtrR;
  const scriptProcessorBufferSize = 1024;
  const maxProcessFrames = scriptProcessorBufferSize;
  const f32 = () => module.HEAPF32;

  const api = {
    async loadWasm() {
      module = await createModule();
      inPtrL = module._malloc(maxProcessFrames * 4); inPtrR = module._malloc(maxProcessFrames * 4);
      outPtrL = module._malloc(maxProcessFrames * 4); outPtrR = module._malloc(maxProcessFrames * 4);
      setStatus('WASM loaded');
    },
    async startAudio() {
      if (!module) throw new Error('Load WASM first');
      ctx = new AudioContext();
      module._DimensionWasm_Init(ctx.sampleRate);
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      const src = ctx.createMediaStreamSource(stream);
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
      src.connect(node).connect(ctx.destination);
      setStatus('audio active');
    },
    setParam(id, value) { if (module) module._DimensionWasm_SetParam(id, value); },
    setMode(mode) { if (module) module._DimensionWasm_SetMode(mode); },
    toggleBypass() { bypass = !bypass; },
    isBypassed() { return bypass; }
  };
  return api;
}
