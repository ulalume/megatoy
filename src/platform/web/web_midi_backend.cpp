#include "platform/web/web_midi_backend.hpp"

#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include <emscripten.h>
#include <emscripten/val.h>
#endif
#include <vector>

namespace {
platform::web::WebMidiBackend *g_active_backend = nullptr;
} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void megatoy_web_midi_message(int source, int status,
                                                   int data1, int data2) {
  if (g_active_backend == nullptr) {
    return;
  }
  g_active_backend->deliver(static_cast<unsigned char>(source),
                            static_cast<unsigned char>(status),
                            static_cast<unsigned char>(data1),
                            static_cast<unsigned char>(data2));
}

EMSCRIPTEN_KEEPALIVE void megatoy_web_midi_closed(int source) {
  if (g_active_backend != nullptr) {
    g_active_backend->closed(static_cast<unsigned char>(source));
  }
}

} // extern "C"

namespace platform::web {

void WebMidiBackend::deliver(unsigned char source, unsigned char status,
                             unsigned char data1, unsigned char data2) {
  MidiMessage message;
  message.source = source;
  if (parse_midi_message(status, data1, data2, message)) {
    emit(message);
  }
}

void WebMidiBackend::closed(unsigned char source) {
  MidiMessage message;
  message.type = MidiMessage::Type::SourceClosed;
  message.source = source;
  emit(message);
}

namespace {
#if defined(MEGATOY_PLATFORM_WEB)
void request_access_js() {
  // clang-format off
  EM_ASM({
    var state = Module['megatoyMidiState'];
    if (!state || !state.available) {
      return;
    }
    if (state.pending || state.status === "enabled") {
      return;
    }
    state.pending = true;
    state.status = "pending";
    state.error = "";

    function installHandlers(access) {
      Module['megatoyMidiAccess'] = access;
      // Each input plays as a source of its own, numbered from 1; those past
      // the last number share it.
      var sources = {};
      function sourceOf(port) {
        if (!sources[port.id]) {
          var taken = Object.keys(sources).map(function(id) {
            return sources[id];
          });
          var source = 1;
          while (source < $0 && taken.indexOf(source) >= 0) {
            source++;
          }
          sources[port.id] = source;
        }
        return sources[port.id];
      }
      function refreshPorts() {
        Module['megatoyMidiPorts'] = [];
        access.inputs.forEach(function(input) {
          Module['megatoyMidiPorts'].push(input.name || input.id || 'MIDI');
        });
        Module['megatoyMidiPortsChanged'] = true;
      }

      function handleMessage(event) {
        // Dispatched immediately rather than queued for the next rendered
        // frame. Emscripten runs the audio callback on this same thread, so
        // there is nothing to race with.
        var d = event.data || [];
        if (d.length < 2 || !Module['_megatoy_web_midi_message']) {
          return;
        }
        var type = d[0] & 0xf0;
        if (type !== 0x80 && type !== 0x90 && type !== 0xb0 && type !== 0xe0) {
          return;
        }
        Module['_megatoy_web_midi_message'](sourceOf(event.target), d[0], d[1],
                                            d.length > 2 ? d[2] : 0);
      }

      access.inputs.forEach(
          function(input) { input.onmidimessage = handleMessage; });

      access.onstatechange = function(event) {
        var port = event.port;
        if (port && port.type === 'input') {
          if (port.state === 'connected') {
            port.onmidimessage = handleMessage;
          } else if (sources[port.id]) {
            // What it held is let go, and its number is free again.
            if (Module['_megatoy_web_midi_closed']) {
              Module['_megatoy_web_midi_closed'](sources[port.id]);
            }
            delete sources[port.id];
          }
        }
        refreshPorts();
      };

      refreshPorts();
    }

    navigator.requestMIDIAccess()
        .then(function(access) {
          state.pending = false;
          state.status = "enabled";
          installHandlers(access);
        })
        .catch(function(err) {
          state.pending = false;
          state.status = "error";
          state.error =
              (err && err.message) ? err.message : "Failed to access WebMIDI.";
        });
  }, MidiBackend::kLastSource);
  // clang-format on
}
#endif
} // namespace

void WebMidiBackend::setup_js_state() const {
#if defined(MEGATOY_PLATFORM_WEB)
  // clang-format off
  EM_ASM({
    if (Module['megatoyMidiSetup']) {
      return;
    }
    Module['megatoyMidiSetup'] = true;
    Module['megatoyMidiPorts'] = [];
    Module['megatoyMidiPortsChanged'] = false;

    var available = typeof navigator !== 'undefined' &&
        typeof navigator.requestMIDIAccess === 'function';
    Module['megatoyMidiState'] = {};
    Module['megatoyMidiState'].available = available;
    Module['megatoyMidiState'].status =
        available ? "needs-permission" : "unavailable";
    Module['megatoyMidiState'].pending = false;
    Module['megatoyMidiState'].error = "";
  });
  // clang-format on
#endif
}

WebMidiBackend::WebStatus WebMidiBackend::read_status_from_js() const {
#if defined(MEGATOY_PLATFORM_WEB)
  setup_js_state();
  using emscripten::val;
  WebStatus info;
  val module = val::global("Module");
  val state = module["megatoyMidiState"];
  if (state.isUndefined()) {
    info.state = State::Unavailable;
    info.message = "WebMIDI unavailable.";
    return info;
  }
  std::string status = state["status"].isUndefined()
                           ? "unavailable"
                           : state["status"].as<std::string>();
  std::string error =
      state["error"].isUndefined() ? "" : state["error"].as<std::string>();
  if (status == "enabled") {
    info.state = State::Enabled;
    info.message = "WebMIDI enabled. Awaiting input…";
  } else if (status == "pending") {
    info.state = State::Pending;
    info.message = "Requesting WebMIDI access…";
  } else if (status == "needs-permission") {
    info.state = State::NeedsPermission;
    info.message = "WebMIDI requires permission. Click Enable WebMIDI.";
  } else if (status == "error") {
    info.state = State::Error;
    info.message = error.empty() ? "WebMIDI permission was denied." : error;
  } else {
    info.state = State::Unavailable;
    info.message = "WebMIDI unsupported in this browser.";
  }
  return info;
#else
  return {State::Unavailable, "WebMIDI unavailable on this platform."};
#endif
}

bool WebMidiBackend::initialize() {
  g_active_backend = this;
#if defined(MEGATOY_PLATFORM_WEB)
  setup_js_state();
  return true;
#else
  return false;
#endif
}

void WebMidiBackend::shutdown() {
  if (g_active_backend == this) {
    g_active_backend = nullptr;
  }
}

MidiBackend::StatusInfo WebMidiBackend::status() const {
  auto web_status = read_status_from_js();
  MidiBackend::StatusInfo info;
  info.message = web_status.message;
  info.show_enable_button = web_status.state == State::NeedsPermission ||
                            web_status.state == State::Error;
  info.enable_button_disabled = web_status.state == State::Pending;
  return info;
}

void WebMidiBackend::request_access() {
#if defined(MEGATOY_PLATFORM_WEB)
  setup_js_state();
  request_access_js();
#endif
}

void WebMidiBackend::poll(std::vector<std::string> &available_ports,
                          bool &ports_changed) {
#if defined(MEGATOY_PLATFORM_WEB)
  setup_js_state();
  using emscripten::val;
  val module = val::global("Module");

  val ports_flag = module["megatoyMidiPortsChanged"];
  if (!ports_flag.isUndefined() && ports_flag.as<bool>()) {
    available_ports.clear();
    val ports = module["megatoyMidiPorts"];
    if (!ports.isUndefined()) {
      int len = ports["length"].as<int>();
      for (int i = 0; i < len; ++i) {
        available_ports.push_back(ports[i].as<std::string>());
      }
    }
    module.set("megatoyMidiPortsChanged", val(false));
    ports_changed = true;
  }

  // Notes arrive through megatoy_web_midi_message, not from here.
#else
  (void)available_ports;
  (void)ports_changed;
#endif
}

} // namespace platform::web
