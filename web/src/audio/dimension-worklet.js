const PARAMS = ['inputGain','outputGain','dryGain','wetDirectGain','wetCrossGain','baseDelayMs','depthMs','rateHz','hpfHz','lpfHz','analogAmount','companderAmount','width'];
const PARAM_DEFAULTS = [1,1,0.83,0.5,0.35,7,0.9,0.25,120,8000,0.35,0.35,1];

function getExportFn(instance, names) {
  const exports = instance?.exports || {};
  for (const name of names) {
    const fn = exports[name];
    if (typeof fn === 'function') return fn;
  }
  throw new Error(`Função WASM não encontrada: ${names.join(' ou ')}`);
}

function resolveAllocatorFns(instance) {
  return {
    malloc: getExportFn(instance, ['_malloc', 'malloc']),
    free: getExportFn(instance, ['_free', 'free'])
  };
}


class DimensionProcessor extends AudioWorkletProcessor {
  static get parameterDescriptors() {
    return PARAMS.map((name, i) => ({ name, defaultValue: PARAM_DEFAULTS[i], automationRate: 'k-rate' }));
  }

  constructor() {
    super();
    this.wasmInstance = null; this.ready = false; this.bypass = false; this.ptrs = null; this.maxFrames = 0; this.maxRenderFrames = 2048; this.silence = new Float32Array(this.maxRenderFrames);
    this.modulePromise = null;
    this.alloc = null;
    this.heap = null;
    this.lastParams = Array(PARAMS.length).fill(NaN);
    this.port.onmessage = async (event) => {
      const msg = event.data || {};
      try {
        if (msg.type === 'init') await this.initWasm(msg.sampleRate || sampleRate, msg.requestId, msg.wasmBytes);
        else if (msg.type === 'setParam' && this.wasmInstance) {
          const paramId = msg.paramId | 0;
          const value = Number(msg.value);
          if (paramId >= 0 && paramId < this.lastParams.length) this.lastParams[paramId] = value;
          getExportFn(this.wasmInstance, ['_DimensionWasm_SetParam', 'DimensionWasm_SetParam'])(paramId, value);
        }
        else if (msg.type === 'setMode' && this.wasmInstance) getExportFn(this.wasmInstance, ['_DimensionWasm_SetMode', 'DimensionWasm_SetMode'])(msg.mode | 0);
        else if (msg.type === 'setBypass') this.bypass = !!msg.value;
        else if (msg.type === 'reset' && this.wasmInstance) getExportFn(this.wasmInstance, ['_DimensionWasm_Reset', 'DimensionWasm_Reset'])();
      } catch (err) {
        this.port.postMessage({ type: 'error', message: err?.message || String(err), requestId: msg.requestId ?? null });
      }
    };
  }

  freeHeapBuffers() { if (!this.wasmInstance || !this.ptrs || !this.alloc) return; Object.values(this.ptrs).forEach((p) => this.alloc.free(p)); this.ptrs = null; this.maxFrames = 0; }
  ensureCapacity(frames) { if (!this.wasmInstance || !this.alloc || (this.ptrs && this.maxFrames >= frames)) return; this.freeHeapBuffers(); const b = frames * 4; this.ptrs = { inPtrL:this.alloc.malloc(b), inPtrR:this.alloc.malloc(b), outPtrL:this.alloc.malloc(b), outPtrR:this.alloc.malloc(b)}; this.maxFrames = frames; }

  async initWasm(sr, requestId, wasmBytes) {
    this.ready = false;
    if (!this.wasmInstance) {
      if (!this.modulePromise) {
        if (!wasmBytes) throw new Error('WASM bytes ausentes na inicialização');
        this.modulePromise = WebAssembly.instantiate(wasmBytes);
      }
      try {
        const instantiated = await this.modulePromise;
        this.wasmInstance = instantiated?.instance ?? instantiated;
      } catch (err) {
        this.modulePromise = null;
        throw err;
      }
    }
    this.freeHeapBuffers();
    this.alloc = resolveAllocatorFns(this.wasmInstance);
    this.heap = new Float32Array(this.wasmInstance.exports.memory.buffer);
    this.lastParams = Array(PARAMS.length).fill(NaN);
    getExportFn(this.wasmInstance, ['_DimensionWasm_Init', 'DimensionWasm_Init'])(sr);
    this.ensureCapacity(this.maxRenderFrames);
    this.ready = true;
    this.port.postMessage({ type: 'WASM_READY', requestId: requestId ?? null });
  }

  syncParams(parameters) {
    if (!this.wasmInstance) return;
    for (let i = 0; i < PARAMS.length; i += 1) {
      const value = parameters[PARAMS[i]]?.[0] ?? this.lastParams[i];
      if (!Object.is(value, this.lastParams[i])) {
        this.lastParams[i] = value;
        getExportFn(this.wasmInstance, ['_DimensionWasm_SetParam', 'DimensionWasm_SetParam'])(i, value);
      }
    }
  }

  process(inputs, outputs, parameters) {
    const input = inputs[0]; const output = outputs[0]; if (!output || output.length < 2) return true;
    const outL = output[0]; const outR = output[1];
    if (!this.ready || !this.wasmInstance) { outL.fill(0); outR.fill(0); return true; }
    this.syncParams(parameters);
    const frames = outL.length;
    if (frames > this.maxFrames) {
      outL.fill(0); outR.fill(0);
      this.port.postMessage({ type: 'error', message: `bloco de áudio maior que capacidade pré-alocada (${frames} > ${this.maxFrames})` });
      return true;
    }

    if (!this.heap || this.heap.buffer !== this.wasmInstance.exports.memory.buffer) {
        this.heap = new Float32Array(this.wasmInstance.exports.memory.buffer);
    }
    const heap = this.heap;

    const inL = input?.[0];
    const inR = input?.[1] || inL;
    const inLPtr = this.ptrs.inPtrL >> 2;
    const inRPtr = this.ptrs.inPtrR >> 2;

    if (inL) {
        heap.set(inL, inLPtr);
    } else {
        heap.fill(0, inLPtr, inLPtr + frames);
    }

    if (inR) {
        heap.set(inR, inRPtr);
    } else {
        heap.fill(0, inRPtr, inRPtr + frames);
    }

    getExportFn(this.wasmInstance, ['_DimensionWasm_Process', 'DimensionWasm_Process'])(this.ptrs.inPtrL,this.ptrs.inPtrR,this.ptrs.outPtrL,this.ptrs.outPtrR,frames,this.bypass ? 1 : 0);

    // Copy out of wasm memory explicitly
    const outLPtr = this.ptrs.outPtrL >> 2;
    const outRPtr = this.ptrs.outPtrR >> 2;
    for (let i = 0; i < frames; i++) {
        outL[i] = heap[outLPtr + i];
        outR[i] = heap[outRPtr + i];
    }

    return true;
  }
}

registerProcessor('dimension-processor', DimensionProcessor);
