#include "platform/web/web_midi_backend.hpp"

#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include <emscripten.h>
#include <emscripten/val.h>
#endif
#include <vector>

namespace platform::web {

namespace {
#if defined(MEGATOY_PLATFORM_WEB)
void request_access_js() {
  EM_ASM({
    var state = Module['megatoyMidiState'];
    if (!state || !state.available) {
      return;
    }
    if (state.pending || state.status == = "enabled") {
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
        Module['megatoyMidiEvents'].push({
          port : event.target && event.target.name ? event.target.name : 'MIDI',
          data : Array.prototype.slice.call(event.data || [])
        });
      }

      access.inputs.forEach(
          function(input) { input.onmidimessage = handleMessage; });

      access.onstatechange = function(event) {
        if (event.port &&event.port.type == = 'input' &&event.port.state ==
            = 'connected') {
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
  });
}
#endif
} // namespace

void WebMidiBackend::setup_js_state() const {
#if defined(MEGATOY_PLATFORM_WEB)
  EM_ASM({
    if (Module['megatoyMidiSetup']) {
      return;
    }
    Module['megatoyMidiSetup'] = true;
    Module['megatoyMidiEvents'] = [];
    Module['megatoyMidiPorts'] = [];
    Module['megatoyMidiPortsChanged'] = false;

    var available = typeof navigator !=
        = 'undefined' &&typeof navigator.requestMIDIAccess == = 'function';
    Module['megatoyMidiState'] = {};
    Module['megatoyMidiState'].available = available;
    Module['megatoyMidiState'].status =
        available ? "needs-permission" : "unavailable";
    Module['megatoyMidiState'].pending = false;
    Module['megatoyMidiState'].error = "";
  });
#endif
}

WebMidiBackend::StatusInfo WebMidiBackend::read_status_from_js() const {
#if defined(MEGATOY_PLATFORM_WEB)
  setup_js_state();
  using emscripten::val;
  StatusInfo info;
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
#if defined(MEGATOY_PLATFORM_WEB)
  setup_js_state();
  return true;
#else
  return false;
#endif
}

void WebMidiBackend::shutdown() {}

WebMidiBackend::StatusInfo WebMidiBackend::status() const {
  return read_status_from_js();
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

  val queue = module["megatoyMidiEvents"];
  if (!queue.isUndefined()) {
    while (queue["length"].as<int>() > 0) {
      val evt = queue.call<val>("shift");
      val data = evt["data"];
      if (data.isUndefined()) {
        continue;
      }
      int len = data["length"].as<int>();
      if (len < 2) {
        continue;
      }
      std::vector<unsigned char> bytes;
      bytes.reserve(len);
      for (int i = 0; i < len; ++i) {
        bytes.push_back(static_cast<unsigned char>(data[i].as<int>()));
      }
      MidiMessage message;
      unsigned char status = bytes[0];
      unsigned char type = status & 0xF0;
      unsigned char midi_note = (len > 1) ? bytes[1] : 0;
      unsigned char velocity = (len > 2) ? bytes[2] : 0;
      message.note = ym2612::Note::from_midi_note(midi_note);
      message.velocity = velocity;
      message.port_name = evt["port"].isUndefined()
                              ? std::string("MIDI")
                              : evt["port"].as<std::string>();
      if (type == 0x90 && velocity > 0) {
        message.type = MidiMessage::Type::NoteOn;
        events.push_back(message);
      } else if (type == 0x80 || (type == 0x90 && velocity == 0)) {
        message.type = MidiMessage::Type::NoteOff;
        events.push_back(message);
      }
    }
  }
#else
  (void)events;
  (void)available_ports;
  (void)ports_changed;
#endif
}

} // namespace platform::web
