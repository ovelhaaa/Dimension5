class DimensionProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.module = null;
    this.ready = false;
    this.bypass = false;
    this.ptrs = null;
    this.maxFrames = 128;

    this.port.onmessage = async (event) => {
      const msg = event.data || {};
      try {
        if (msg.type === 'init') {
          await this.initWasm(msg.moduleUrl, msg.sampleRate || sampleRate);
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
        this.port.postMessage({ type: 'error', message: err?.message || String(err) });
      }
    };
  }

  async initWasm(moduleUrl, sr) {
    const factoryImport = await import(moduleUrl);
    const createModule = factoryImport.default;
    this.module = await createModule();

    const bytes = this.maxFrames * 4;
    const inPtrL = this.module._malloc(bytes);
    const inPtrR = this.module._malloc(bytes);
    const outPtrL = this.module._malloc(bytes);
    const outPtrR = this.module._malloc(bytes);
    this.ptrs = { inPtrL, inPtrR, outPtrL, outPtrR };

    this.module._DimensionWasm_Init(sr);
    this.ready = true;
    this.port.postMessage({ type: 'ready' });
  }

  process(inputs, outputs) {
    const input = inputs[0];
    const output = outputs[0];
    if (!output || output.length < 2) return true;

    const outL = output[0];
    const outR = output[1];

    if (!this.ready || !this.module || !this.ptrs) {
      outL.fill(0);
      outR.fill(0);
      return true;
    }

    const inL = input?.[0] || outL;
    const inR = input?.[1] || inL;
    const frames = inL.length;
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
