// Plays the stereo frames the emulator produces. Each message is a
// Float32Array of interleaved L/R samples at the context's rate (44.1 kHz).
class VccAudio extends AudioWorkletProcessor {
  constructor() {
    super();
    this.queue = [];
    this.current = null;
    this.pos = 0;
    this.queued = 0;
    this.port.onmessage = (e) => {
      if (e.data === 'clear') { this.queue = []; this.current = null; this.queued = 0; return; }
      this.queue.push(e.data);
      this.queued += e.data.length / 2;
      // Keep latency bounded: drop the oldest audio past ~150 ms.
      while (this.queued > sampleRate * 0.15 && this.queue.length > 1) {
        this.queued -= this.queue.shift().length / 2;
      }
    };
  }
  process(inputs, outputs) {
    const out = outputs[0];
    const l = out[0], r = out[1] || out[0];
    for (let i = 0; i < l.length; i++) {
      if (!this.current || this.pos >= this.current.length) {
        this.current = this.queue.shift() || null;
        this.pos = 0;
        if (!this.current) { l[i] = 0; r[i] = 0; continue; }
        this.queued -= this.current.length / 2;
      }
      l[i] = this.current[this.pos++];
      r[i] = this.current[this.pos++];
    }
    return true;
  }
}
registerProcessor('vcc-audio', VccAudio);
