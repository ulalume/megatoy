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
    this.scope = new Float32Array(this.reportFrames * 2);
    this.filled = 0;
    // Rendering time is summed over a few reports before it is sent: an audio
    // worklet's clock may tick only every millisecond.
    this.elapsed = 0;
    this.timed = 0;
    this.pending = [];
    this.port.onmessage = (event) => this.pending.push(event.data);
    this.port.postMessage({ type: 'ready', ok: this.ready });
  }

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
        engine.mt_submit();
        break;
      }
      case 'noteOptions':
        engine.mt_set_note_options(message.velocity, message.depth,
                                   message.steal);
        break;
      case 'performance':
        engine.mt_set_performance_options(message.bend, message.wheel);
        break;
    }
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    if (!this.ready || output.length === 0) {
      return true;
    }
    const started = now();
    for (const message of this.pending) {
      this.apply(message);
    }
    this.pending.length = 0;

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

    let to = this.filled * 2;
    for (let i = 0; i < frames && to < this.scope.length; ++i) {
      this.scope[to++] = left[i];
      this.scope[to++] = right[i];
    }
    this.filled = Math.min(this.filled + frames, this.reportFrames);
    this.elapsed += now() - started;
    this.timed += frames;

    if (this.filled >= this.reportFrames) {
      this.report();
    }
    return true;
  }

  // What the page draws: the output since the last report, the engine's
  // notes and voices, and now and then how long rendering took.
  report() {
    const engine = this.engine;
    const snapshot = new Uint8Array(engine.memory.buffer, engine.mt_snapshot(),
                                    engine.mt_snapshot_size()).slice();
    const scope = this.scope;
    const timed = this.timed >= this.loadFrames;
    this.port.postMessage({
      type: 'report',
      frames: this.filled,
      scope: scope.buffer,
      snapshot: snapshot.buffer,
      timedFrames: timed ? this.timed : 0,
      elapsedMs: timed ? this.elapsed : 0,
    }, [scope.buffer, snapshot.buffer]);
    this.scope = new Float32Array(this.reportFrames * 2);
    this.filled = 0;
    if (timed) {
      this.elapsed = 0;
      this.timed = 0;
    }
  }
}

registerProcessor('megatoy', MegatoyProcessor);
