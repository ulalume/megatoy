// The page's side of megatoy's sound: the browser's player, the audio
// worklet that renders into it (dist/web_audio_worklet.js), and the messages
// between them. src/platform/web/web_audio_worklet.cpp drives it.
Module['megatoyAudio'] = (() => {
  const audio = {
    context: null,
    node: null,
    // Messages for a worklet still loading, sent once it has.
    queue: [],
    // Each start counts, so a worklet that finishes loading after a newer
    // start is dropped rather than connected.
    generation: 0,
    wasm: null,
  };

  const locate = (name) =>
    typeof Module['locateFile'] === 'function' ? Module['locateFile'](name, '')
                                               : name;

  // A browser lets a player start only from a touch, a click or a key, and a
  // phone stops it when it sleeps or the tab is hidden; every one of those,
  // and the page showing again, tries it again.
  const resume = () => {
    if (audio.context && audio.context.state !== 'running' &&
        audio.context.state !== 'closed') {
      audio.context.resume().catch(() => {});
    }
  };
  for (const type of ['pointerdown', 'pointerup', 'touchend', 'mousedown',
                      'keydown']) {
    document.addEventListener(type, resume, true);
  }
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) {
      resume();
    }
  });

  const receive = (data) => {
    if (data.type === 'report') {
      const frames = data.frames;
      const scope = new Float32Array(data.scope, 0, frames * 2);
      HEAPF32.set(scope, _megatoy_audio_scope_inbox(frames) >> 2);
      const snapshot = new Uint8Array(data.snapshot);
      HEAPU8.set(snapshot, _megatoy_audio_snapshot_inbox());
      _megatoy_audio_report(frames, data.timedFrames, data.elapsedMs);
    } else if (data.type === 'ready' && !data.ok) {
      console.error('megatoy: the audio worklet did not start');
    }
  };

  /// Opens a player and starts loading the worklet into it. Returns the
  /// player's sample rate, or 0 where there is no worklet to be had.
  audio.start = (settings) => {
    const Context = globalThis.AudioContext || globalThis.webkitAudioContext;
    if (!Context || !globalThis.AudioWorkletNode) {
      return 0;
    }
    // Plays through the ringer switch on an iPhone, as other players do.
    try {
      if (navigator.audioSession) {
        navigator.audioSession.type = 'playback';
      }
    } catch (error) {
    }
    const context = new Context({latencyHint: 'interactive'});
    // Only in a secure context: https, or this computer.
    if (!context.audioWorklet) {
      context.close().catch(() => {});
      return 0;
    }
    const generation = ++audio.generation;
    audio.context = context;
    audio.node = null;
    audio.wasm = audio.wasm ||
                 fetch(locate('megatoy-audio.wasm')).then((response) => {
                   if (!response.ok) {
                     throw new Error('megatoy-audio.wasm: ' + response.status);
                   }
                   return response.arrayBuffer();
                 });
    Promise.all([audio.wasm,
                 context.audioWorklet.addModule(
                     locate('megatoy-audio-worklet.js'))])
        .then(([wasm]) => {
          if (generation !== audio.generation) {
            return;
          }
          const node = new AudioWorkletNode(context, 'megatoy', {
            numberOfInputs: 0,
            numberOfOutputs: 1,
            outputChannelCount: [2],
            processorOptions: Object.assign({wasm: wasm.slice(0)}, settings),
          });
          node.port.onmessage = (event) => receive(event.data);
          node.connect(context.destination);
          audio.node = node;
          for (const message of audio.queue) {
            node.port.postMessage(message);
          }
          audio.queue = [];
        })
        .catch((error) => {
          audio.wasm = null;
          console.error('megatoy: audio did not start', error);
        });
    return context.sampleRate;
  };

  /// Closes the player, and with it the worklet and its engine.
  audio.stop = () => {
    ++audio.generation;
    if (audio.node) {
      audio.node.port.onmessage = null;
      audio.node.disconnect();
    }
    if (audio.context && audio.context.state !== 'closed') {
      audio.context.close().catch(() => {});
    }
    audio.context = null;
    audio.node = null;
    audio.queue = [];
  };

  audio.post = (message) => {
    if (audio.node) {
      audio.node.port.postMessage(message);
    } else {
      audio.queue.push(message);
    }
  };

  audio.playing = () =>
      audio.context !== null && audio.context.state === 'running';

  return audio;
})();
