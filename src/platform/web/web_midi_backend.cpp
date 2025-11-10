#include "platform/web/web_midi_backend.hpp"

#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include <emscripten.h>
#include <emscripten/val.h>
#include <vector>

namespace {

void initialize_js_backend() {
  EM_ASM({
    if (Module['megatoyMidiSetup']) {
      return;
    }
    Module['megatoyMidiSetup'] = true;
    Module['megatoyMidiEvents'] = [];
    Module['megatoyMidiPorts'] = [];
    Module['megatoyMidiPortsChanged'] = false;

    if (typeof navigator === 'undefined' ||
        typeof navigator.requestMIDIAccess !== 'function') {
      console.warn('WebMIDI not available in this browser.');
      return;
    }

    navigator.requestMIDIAccess().then(function (access) {
      function refreshPorts() {
        Module['megatoyMidiPorts'] = [];
        access.inputs.forEach(function (input) {
          Module['megatoyMidiPorts'].push(input.name || input.id || 'MIDI');
        });
        Module['megatoyMidiPortsChanged'] = true;
      }

      function handleMessage(event) {
        Module['megatoyMidiEvents'].push({
          port: event.target && event.target.name ? event.target.name : 'MIDI',
          data: Array.prototype.slice.call(event.data || [])
        });
      }

      access.inputs.forEach(function (input) {
        input.onmidimessage = handleMessage;
      });

      access.onstatechange = function (event) {
        if (event.port.type === 'input' && event.port.state === 'connected') {
          event.port.onmidimessage = handleMessage;
        }
        refreshPorts();
      };

      refreshPorts();
    }, function (err) {
      console.warn('WebMIDI request failed', err);
    });
  });
}

} // namespace

#endif

namespace platform::web {

bool WebMidiBackend::initialize() {
#if defined(MEGATOY_PLATFORM_WEB)
  initialize_js_backend();
  return true;
#else
  return false;
#endif
}

void WebMidiBackend::shutdown() {}

void WebMidiBackend::poll(std::vector<MidiMessage> &events,
                          std::vector<std::string> &available_ports,
                          bool &ports_changed) {
#if defined(MEGATOY_PLATFORM_WEB)
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
      message.port_name =
          evt["port"].isUndefined() ? std::string("MIDI")
                                    : evt["port"].as<std::string>();
      if (type == 0x90 && velocity > 0) {
        message.type = MidiMessage::Type::NoteOn;
        events.push_back(message);
      } else if (type == 0x80 ||
                 (type == 0x90 && velocity == 0)) {
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
