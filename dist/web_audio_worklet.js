// megatoy's sound on the browser's audio thread. The engine is
// megatoy-audio.wasm (src/platform/web/audio_worklet/worklet_engine.cpp); the
// page sends it commands and receives what it shows, as messages, so nothing
// the page does can make this thread wait. See
// src/platform/web/web_audio_worklet.cpp for the page's side.

const now = () =>
  globalThis.performance ? globalThis.performance.now() : Date.now();

// The engine needs a clock and somewhere to print to, nothing else of WASI.
function wasiImports(memory) {
  return {
    clock_time_get(_id, _precision, out) {
      const view = new DataView(memory().buffer);
      view.setBigUint64(out, BigInt(Math.round(now() * 1e6)), true);
      return 0;
    },
    fd_write(_fd, iovs, count, written) {
      const view = new DataView(memory().buffer);
      let total = 0;
      for (let i = 0; i < count; ++i) {
        total += view.getUint32(iovs + i * 8 + 4, true);
      }
      view.setUint32(written, total, true);
      return 0;
    },
    proc_exit(code) {
      throw new Error('megatoy audio exited with ' + code);
    },
  };
}

class MegatoyProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const settings = options.processorOptions;
    const module = new WebAssembly.Module(settings.wasm);
    let instance = null;
    const memory = () => instance.exports.memory;
    const wasi = wasiImports(memory);
    const imports = {};
    for (const entry of WebAssembly.Module.imports(module)) {
      imports[entry.module] = imports[entry.module] || {};
      if (entry.kind === 'function') {
        imports[entry.module][entry.name] = wasi[entry.name] || (() => 0);
      }
    }
    instance = new WebAssembly.Instance(module, imports);
    const engine = (this.engine = instance.exports);
    if (engine._initialize) {
      engine._initialize();
    }

    this.ready =
      engine.mt_protocol_version() === settings.protocol &&
      engine.mt_command_size() === settings.commandSize &&
      engine.mt_snapshot_size() === settings.snapshotSize &&
      engine.mt_init(sampleRate) === 1;
    this.reportFrames = settings.reportFrames;
    this.loadFrames = settings.loadFrames;
    this.snapshotSize = settings.snapshotSize;
    // A report is one buffer: the output since the last one, interleaved,
    // then the engine's snapshot. The page hands each back once it has read
    // it, so reports allocate nothing once a few are in circulation.
    this.reportBytes = this.reportFrames * 8 + this.snapshotSize;
    this.spare = [];
    this.report = this.takeBuffer();
    this.filled = 0;
    // Rendering time is summed over a few reports before it is sent: an audio
    // worklet's clock may tick only every millisecond.
    this.elapsed = 0;
    this.timed = 0;
    this.pending = [];
    this.port.onmessage = (event) => this.pending.push(event.data);
    this.port.postMessage({ type: 'ready', ok: this.ready });
  }

  takeBuffer() {
    const buffer = this.spare.pop() || new ArrayBuffer(this.reportBytes);
    return {
      buffer: buffer,
      scope: new Float32Array(buffer, 0, this.reportFrames * 2),
    };
  }

  /// Applies one message; false for a command the engine's queue has no
  /// room for until it renders.
  apply(message) {
    const engine = this.engine;
    switch (message.type) {
      case 'command': {
        const bytes = new Uint8Array(message.bytes);
        if (!this.inbox || this.inbox.length !== bytes.length) {
          this.inbox =
              new Uint8Array(engine.memory.buffer, engine.mt_inbox(), bytes.length);
        }
        this.inbox.set(bytes);
        return engine.mt_submit() === 1;
      }
      case 'noteOptions':
        engine.mt_set_note_options(message.velocity, message.depth,
                                   message.steal);
        return true;
      case 'performance':
        engine.mt_set_performance_options(message.bend, message.wheel);
        return true;
      case 'recycle':
        if (message.buffer.byteLength === this.reportBytes) {
          this.spare.push(message.buffer);
        }
        return true;
    }
    return true;
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    if (!this.ready || output.length === 0) {
      return true;
    }
    const started = now();
    // In order, as far as the engine takes them: what is left, after a
    // stretch the player was stopped for, waits for the next block.
    let applied = 0;
    while (applied < this.pending.length && this.apply(this.pending[applied])) {
      ++applied;
    }
    this.pending.splice(0, applied);

    // The engine's memory never grows, so views into it stay good for as
    // long as it keeps rendering into the same place.
    const frames = output[0].length;
    const at = this.engine.mt_render(frames);
    if (at !== this.renderedAt || frames !== this.renderedFrames) {
      const rendered =
          new Float32Array(this.engine.memory.buffer, at, frames * 2);
      this.left = rendered.subarray(0, frames);
      this.right = rendered.subarray(frames);
      this.renderedAt = at;
      this.renderedFrames = frames;
    }
    const left = this.left;
    const right = this.right;
    output[0].set(left);
    if (output.length > 1) {
      output[1].set(right);
    }

    const scope = this.report.scope;
    let to = this.filled * 2;
    for (let i = 0; i < frames && to < scope.length; ++i) {
      scope[to++] = left[i];
      scope[to++] = right[i];
    }
    this.filled = Math.min(this.filled + frames, this.reportFrames);
    this.elapsed += now() - started;
    this.timed += frames;

    if (this.filled >= this.reportFrames) {
      this.send();
    }
    return true;
  }

  // What the page draws: the output since the last report, the engine's
  // notes and voices, and now and then how long rendering took.
  send() {
    const engine = this.engine;
    const buffer = this.report.buffer;
    const at = engine.mt_snapshot();
    if (at !== this.snapshotAt) {
      this.snapshot = new Uint8Array(engine.memory.buffer, at, this.snapshotSize);
      this.snapshotAt = at;
    }
    new Uint8Array(buffer, this.reportFrames * 8, this.snapshotSize)
        .set(this.snapshot);
    const timed = this.timed >= this.loadFrames;
    this.port.postMessage({
      type: 'report',
      frames: this.filled,
      buffer: buffer,
      timedFrames: timed ? this.timed : 0,
      elapsedMs: timed ? this.elapsed : 0,
    }, [buffer]);
    this.report = this.takeBuffer();
    this.filled = 0;
    if (timed) {
      this.elapsed = 0;
      this.timed = 0;
    }
  }
}

registerProcessor('megatoy', MegatoyProcessor);
