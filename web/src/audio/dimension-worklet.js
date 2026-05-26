import createDimensionModule from '/wasm/dimension_dsp.js';

function getWasmBasePath() {
  try {
    const workletUrl = new URL(import.meta.url);
    const queryBase = workletUrl.searchParams.get('base');
    if (queryBase) return new URL(`wasm/`, queryBase).toString();

    const assetsIndex = workletUrl.pathname.indexOf('/assets/');
    if (assetsIndex !== -1) {
      return `${workletUrl.origin}${workletUrl.pathname.substring(0, assetsIndex + 1)}wasm/`;
    }
  } catch (_) {
    // fallback abaixo
  }
  return '/wasm/';
}

const WASM_BASE_PATH = getWasmBasePath();

class DimensionProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.module = null;
    this.ready = false;
    this.bypass = false;
    this.ptrs = null;
    this.maxFrames = 0;

    this.port.onmessage = async (event) => {
      const msg = event.data || {};
      try {
        if (msg.type === 'init') {
          await this.initWasm(msg.sampleRate || sampleRate, msg.requestId);
        } else if (msg.type === 'setParam' && this.module) {
          this.module._DimensionWasm_SetParam(msg.paramId | 0, Number(msg.value));
        } else if (msg.type === 'setMode' && this.module) {
          this.module._DimensionWasm_SetMode(msg.mode | 0);
        } else if (msg.type === 'setBypass') {
          this.bypass = !!msg.value;
        } else if (msg.type === 'reset' && this.module) {
          this.module._DimensionWasm_Reset();
        }
      } catch (err) {
        if (!err?.__alreadyReportedToMainThread) {
          this.port.postMessage({ type: 'error', message: err?.message || String(err), requestId: msg.requestId ?? null });
        }
      }
    };
  }

  freeHeapBuffers() {
    if (!this.module || !this.ptrs) return;
    this.module._free(this.ptrs.inPtrL);
    this.module._free(this.ptrs.inPtrR);
    this.module._free(this.ptrs.outPtrL);
    this.module._free(this.ptrs.outPtrR);
    this.ptrs = null;
    this.maxFrames = 0;
  }

  ensureCapacity(frames) {
    if (!this.module) return;
    if (this.ptrs && this.maxFrames >= frames) return;

    this.freeHeapBuffers();

    const bytes = frames * 4;
    this.ptrs = {
      inPtrL: this.module._malloc(bytes),
      inPtrR: this.module._malloc(bytes),
      outPtrL: this.module._malloc(bytes),
      outPtrR: this.module._malloc(bytes)
    };
    this.maxFrames = frames;
  }

  async initWasm(sr, requestId) {
    this.ready = false;
    try {
      if (!this.module) {
        this.module = await createDimensionModule({
          locateFile: (path) => `${WASM_BASE_PATH}${path}`
        });
      }
    } catch (err) {
      const reportedError = err instanceof Error ? err : new Error(String(err));
      reportedError.__alreadyReportedToMainThread = true;
      this.port.postMessage({ type: 'error', requestId: requestId ?? null, message: reportedError.message });
      throw reportedError;
    }

    this.freeHeapBuffers();
    this.module._DimensionWasm_Init(sr);
    this.ready = true;
    this.port.postMessage({ type: 'ready', requestId: requestId ?? null });
  }

  process(inputs, outputs) {
    const input = inputs[0];
    const output = outputs[0];
    if (!output || output.length < 2) return true;

    const outL = output[0];
    const outR = output[1];

    if (!this.ready || !this.module) {
      outL.fill(0);
      outR.fill(0);
      return true;
    }

    const inL = input?.[0] || outL;
    const inR = input?.[1] || inL;
    const frames = inL.length;
    this.ensureCapacity(frames);

    const heap = this.module.HEAPF32;
    heap.set(inL, this.ptrs.inPtrL >> 2);
    heap.set(inR, this.ptrs.inPtrR >> 2);

    this.module._DimensionWasm_Process(
      this.ptrs.inPtrL,
      this.ptrs.inPtrR,
      this.ptrs.outPtrL,
      this.ptrs.outPtrR,
      frames,
      this.bypass ? 1 : 0
    );

    outL.set(heap.subarray(this.ptrs.outPtrL >> 2, (this.ptrs.outPtrL >> 2) + frames));
    outR.set(heap.subarray(this.ptrs.outPtrR >> 2, (this.ptrs.outPtrR >> 2) + frames));
    return true;
  }
}

registerProcessor('dimension-processor', DimensionProcessor);
