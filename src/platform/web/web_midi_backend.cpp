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

EMSCRIPTEN_KEEPALIVE void megatoy_web_midi_message(int status, int note,
                                                   int velocity) {
  if (g_active_backend == nullptr) {
    return;
  }
  g_active_backend->deliver(static_cast<unsigned char>(status),
                            static_cast<unsigned char>(note),
                            static_cast<unsigned char>(velocity));
}

} // extern "C"

namespace platform::web {

void WebMidiBackend::deliver(unsigned char status, unsigned char note,
                             unsigned char velocity) {
  const unsigned char type = status & 0xF0;
  MidiMessage message;
  message.note = ym2612::Note::from_midi_note(note);
  message.velocity = velocity;
  message.port_name = "MIDI";

  if (type == 0x90 && velocity > 0) {
    message.type = MidiMessage::Type::NoteOn;
    emit(message);
  } else if (type == 0x80 || (type == 0x90 && velocity == 0)) {
    message.type = MidiMessage::Type::NoteOff;
    emit(message);
  }
}

namespace {
#if defined(MEGATOY_PLATFORM_WEB)
void request_access_js() {
  EM_ASM({
    // clang-format off
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
        Module['_megatoy_web_midi_message'](d[0], d[1], d.length > 2 ? d[2] : 0);
      }

      access.inputs.forEach(
          function(input) { input.onmidimessage = handleMessage; });

      access.onstatechange = function(event) {
        if (event.port && event.port.type === 'input' &&
            event.port.state === 'connected') {
          event.port.onmidimessage = handleMessage;
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
    // clang-format on
  });
}
#endif
} // namespace

void WebMidiBackend::setup_js_state() const {
#if defined(MEGATOY_PLATFORM_WEB)
  EM_ASM({
    // clang-format off
    if (Module['megatoyMidiSetup']) {
      return;
    }
    Module['megatoyMidiSetup'] = true;
    Module['megatoyMidiEvents'] = [];
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
    // clang-format on
  });
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
  }}

MidiBackend::StatusInfo WebMidiBackend::status() const {
  auto web_status = read_status_from_js();
  MidiBackend::StatusInfo info;
  info.message = web_status.message;
  info.show_enable_button =
      web_status.state == State::NeedsPermission ||
      web_status.state == State::Error;
  info.enable_button_disabled =
      web_status.state == State::Pending;
  return info;
}

void WebMidiBackend::request_access() {
#if defined(MEGATOY_PLATFORM_WEB)
  setup_js_state();
  request_access_js();
#endif
}

void WebMidiBackend::poll(std::vector<MidiMessage> &events,
                          std::vector<std::string> &available_ports,
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
  (void)events;
  (void)available_ports;
  (void)ports_changed;
#endif
}

} // namespace platform::web
