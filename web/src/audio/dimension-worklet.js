import createDimensionModule from '../../public/wasm/dimension_dsp.js';

function getWasmBasePath() {
  try {
    const workletUrl = new URL(import.meta.url);
    const queryBase = workletUrl.searchParams.get('base');
    if (queryBase) return new URL('wasm/', queryBase).toString();
    const assetsIndex = workletUrl.pathname.indexOf('/assets/');
    if (assetsIndex !== -1) return `${workletUrl.origin}${workletUrl.pathname.substring(0, assetsIndex + 1)}wasm/`;
  } catch (_) {}
  return '/wasm/';
}

const WASM_BASE_PATH = getWasmBasePath();
const PARAMS = ['inputGain','outputGain','dryGain','wetDirectGain','wetCrossGain','baseDelayMs','depthMs','rateHz','hpfHz','lpfHz','analogAmount','companderAmount','width'];
const PARAM_DEFAULTS = [1,1,0.83,0.5,0.35,7,0.9,0.25,120,8000,0.35,0.35,1];

class DimensionProcessor extends AudioWorkletProcessor {
  static get parameterDescriptors() {
    return PARAMS.map((name, i) => ({ name, defaultValue: PARAM_DEFAULTS[i], automationRate: 'k-rate' }));
  }

  constructor() {
    super();
    this.module = null; this.ready = false; this.bypass = false; this.ptrs = null; this.maxFrames = 0;
    this.lastParams = Array(PARAMS.length).fill(NaN);
    this.port.onmessage = async (event) => {
      const msg = event.data || {};
      try {
        if (msg.type === 'init') await this.initWasm(msg.sampleRate || sampleRate, msg.requestId, msg.wasmBytes);
        else if (msg.type === 'setParam' && this.module) {
          const paramId = msg.paramId | 0;
          const value = Number(msg.value);
          if (paramId >= 0 && paramId < this.lastParams.length) this.lastParams[paramId] = value;
          this.module._DimensionWasm_SetParam(paramId, value);
        }
        else if (msg.type === 'setMode' && this.module) this.module._DimensionWasm_SetMode(msg.mode | 0);
        else if (msg.type === 'setBypass') this.bypass = !!msg.value;
        else if (msg.type === 'reset' && this.module) this.module._DimensionWasm_Reset();
      } catch (err) {
        this.port.postMessage({ type: 'error', message: err?.message || String(err), requestId: msg.requestId ?? null });
      }
    };
  }

  freeHeapBuffers() { if (!this.module || !this.ptrs) return; Object.values(this.ptrs).forEach((p) => this.module._free(p)); this.ptrs = null; this.maxFrames = 0; }
  ensureCapacity(frames) { if (!this.module || (this.ptrs && this.maxFrames >= frames)) return; this.freeHeapBuffers(); const b = frames * 4; this.ptrs = { inPtrL:this.module._malloc(b), inPtrR:this.module._malloc(b), outPtrL:this.module._malloc(b), outPtrR:this.module._malloc(b)}; this.maxFrames = frames; }

  async initWasm(sr, requestId, wasmBytes) {
    this.ready = false;
    if (!this.module) this.module = await createDimensionModule({ wasmBinary: wasmBytes, locateFile: (path) => `${WASM_BASE_PATH}${path}` });
    this.freeHeapBuffers();
    this.lastParams = Array(PARAMS.length).fill(NaN);
    this.module._DimensionWasm_Init(sr);
    this.ready = true;
    this.port.postMessage({ type: 'ready', requestId: requestId ?? null });
  }

  syncParams(parameters) {
    if (!this.module) return;
    for (let i = 0; i < PARAMS.length; i += 1) {
      const value = parameters[PARAMS[i]]?.[0] ?? this.lastParams[i];
      if (!Object.is(value, this.lastParams[i])) {
        this.lastParams[i] = value;
        this.module._DimensionWasm_SetParam(i, value);
      }
    }
  }

  process(inputs, outputs, parameters) {
    const input = inputs[0]; const output = outputs[0]; if (!output || output.length < 2) return true;
    const outL = output[0]; const outR = output[1];
    if (!this.ready || !this.module) { outL.fill(0); outR.fill(0); return true; }
    this.syncParams(parameters);
    const inL = input?.[0] || outL; const inR = input?.[1] || inL; const frames = inL.length; this.ensureCapacity(frames);
    const heap = this.module.HEAPF32;
    heap.set(inL, this.ptrs.inPtrL >> 2); heap.set(inR, this.ptrs.inPtrR >> 2);
    this.module._DimensionWasm_Process(this.ptrs.inPtrL,this.ptrs.inPtrR,this.ptrs.outPtrL,this.ptrs.outPtrR,frames,this.bypass ? 1 : 0);
    outL.set(heap.subarray(this.ptrs.outPtrL >> 2, (this.ptrs.outPtrL >> 2) + frames));
    outR.set(heap.subarray(this.ptrs.outPtrR >> 2, (this.ptrs.outPtrR >> 2) + frames));
    return true;
  }
}

registerProcessor('dimension-processor', DimensionProcessor);
