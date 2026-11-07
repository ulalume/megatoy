// AudioManager on the web. The engine renders in an audio worklet on the
// browser's audio thread (dist/web_audio_worklet.js running
// megatoy-audio.wasm), so a page busy drawing, or held up by the browser,
// never starves the output. Commands go to it as messages; it reports its
// output, its load and its notes back (dist/web_audio_pre.js), and the UI
// reads the copies kept here.

#include "audio/audio_manager.hpp"

#include "audio/performance.hpp"

#include <algorithm>
#include <cstring>
#include <emscripten.h>
#include <vector>

namespace {

AudioManager *g_manager = nullptr;
std::vector<float> g_scope_inbox;
web_audio::Snapshot g_snapshot_inbox;

// clang-format off
EM_JS(int, worklet_start, (int protocol, int command_size, int snapshot_size,
                           int report_frames, int load_frames), {
  return Module['megatoyAudio'].start({
    protocol: protocol,
    commandSize: command_size,
    snapshotSize: snapshot_size,
    reportFrames: report_frames,
    loadFrames: load_frames,
  });
});

EM_JS(void, worklet_stop, (), { Module['megatoyAudio'].stop(); });

EM_JS(void, worklet_post_command, (const void *bytes, int size), {
  Module['megatoyAudio'].post({
    type: 'command',
    bytes: HEAPU8.slice(bytes, bytes + size).buffer,
  });
});

EM_JS(void, worklet_post_note_options, (int velocity, int depth, int steal), {
  Module['megatoyAudio'].post({
    type: 'noteOptions', velocity: velocity, depth: depth, steal: steal,
  });
});

EM_JS(void, worklet_post_performance, (int bend, int wheel), {
  Module['megatoyAudio'].post({type: 'performance', bend: bend, wheel: wheel});
});
// clang-format on

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE float *megatoy_audio_scope_inbox(uint32_t frames) {
  g_scope_inbox.resize(static_cast<std::size_t>(frames) * 2);
  return g_scope_inbox.data();
}

EMSCRIPTEN_KEEPALIVE void *megatoy_audio_snapshot_inbox() {
  return &g_snapshot_inbox;
}

EMSCRIPTEN_KEEPALIVE void megatoy_audio_report(uint32_t frames,
                                               uint32_t timed_frames,
                                               double elapsed_ms) {
  if (g_manager != nullptr &&
      g_scope_inbox.size() >= static_cast<std::size_t>(frames) * 2) {
    g_manager->receive_report(g_scope_inbox.data(), frames, timed_frames,
                              elapsed_ms, g_snapshot_inbox);
  }
}

} // extern "C"

AudioManager::AudioManager() = default;

// The engine needs no transport here; app_services passes none.
AudioManager::AudioManager(std::unique_ptr<AudioTransport>) {}

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize(uint32_t, int) {
  if (running_) {
    return true;
  }
  g_manager = this;
  const int rate = worklet_start(static_cast<int>(web_audio::kProtocolVersion),
                                 static_cast<int>(sizeof(audio::AudioCommand)),
                                 static_cast<int>(sizeof(web_audio::Snapshot)),
                                 static_cast<int>(web_audio::kReportFrames),
                                 static_cast<int>(web_audio::kLoadFrames));
  if (rate <= 0) {
    return false;
  }
  sample_rate_ = static_cast<uint32_t>(rate);
  snapshot_ = {};
  snapshot_.voices.sample_rate = sample_rate_;
  snapshot_.voices.now_samples = clock_base_;
  scope_.clear();
  load_.clear();
  running_ = true;
  return true;
}

// The worklet renders a fixed 128 frames at a time; how far ahead the browser
// keeps is its own.
bool AudioManager::set_buffer_frames(int) { return true; }

int AudioManager::buffer_frames() const { return 0; }

int AudioManager::default_buffer_frames() const { return 0; }

// Tried again each time the page shows, so one that fails is not the last.
bool AudioManager::reopen_device() {
  if (sample_rate_ == 0) {
    return false; // never had sound to bring back
  }
  if (running_) {
    worklet_stop();
    running_ = false;
  }
  clock_base_ = snapshot_.voices.now_samples;
  for (const auto &voice : snapshot_.voices.voices) {
    sequence_base_ = std::max(sequence_base_, voice.sequence);
  }
  if (!initialize(sample_rate_, 0)) {
    return false;
  }
  resend_state();
  return true;
}

void AudioManager::shutdown() {
  if (running_) {
    worklet_stop();
    running_ = false;
  }
  if (g_manager == this) {
    g_manager = nullptr;
  }
}

void AudioManager::post(const audio::AudioCommand &command) {
  worklet_post_command(&command, static_cast<int>(sizeof(command)));
}

bool AudioManager::submit(const audio::AudioCommand &command) {
  switch (command.type) {
  case audio::AudioCommand::Type::ApplyPatch:
    patch_ = command;
    break;
  case audio::AudioCommand::Type::SetChipType:
    chip_ = command;
    break;
  case audio::AudioCommand::Type::SetCore:
    core_ = command;
    break;
  default:
    break;
  }
  if (!running_) {
    return false;
  }
  post(command);
  return true;
}

bool AudioManager::submit_from_midi(const audio::AudioCommand &command) {
  return submit(command);
}

void AudioManager::set_note_options(bool use_velocity,
                                    int velocity_sensitivity_depth,
                                    bool steal_oldest) {
  const int depth = std::clamp(velocity_sensitivity_depth, 0, 100);
  note_options_ = NoteOptions{use_velocity, depth, steal_oldest};
  worklet_post_note_options(use_velocity, depth, steal_oldest);
}

void AudioManager::set_performance_options(bool pitch_bend, bool mod_wheel) {
  performance_options_ = std::make_pair(pitch_bend, mod_wheel);
  worklet_post_performance(pitch_bend, mod_wheel);
  if (!pitch_bend) {
    submit(
        audio::AudioCommand::pitch_bend(audio::performance::kPitchBendCenter));
  }
  if (!mod_wheel) {
    submit(audio::AudioCommand::mod_wheel(0));
  }
}

void AudioManager::resend_state() {
  if (note_options_) {
    worklet_post_note_options(note_options_->use_velocity, note_options_->depth,
                              note_options_->steal_oldest);
  }
  if (performance_options_) {
    worklet_post_performance(performance_options_->first,
                             performance_options_->second);
  }
  for (const auto *command : {&core_, &chip_, &patch_}) {
    if (*command) {
      post(**command);
    }
  }
}

void AudioManager::receive_report(const float *scope, uint32_t frames,
                                  uint32_t timed_frames, double elapsed_ms,
                                  const web_audio::Snapshot &snapshot) {
  scope_.write(scope, frames);
  if (timed_frames > 0) {
    load_.record(static_cast<std::int64_t>(elapsed_ms * 1'000'000.0),
                 timed_frames, sample_rate_);
  }
  snapshot_ = snapshot;
  snapshot_.voices.now_samples += clock_base_;
  for (auto &voice : snapshot_.voices.voices) {
    if (voice.valid()) {
      voice.sequence += sequence_base_;
      voice.key_on_sample += clock_base_;
      voice.key_off_sample += clock_base_;
    }
  }
}

const audio::ScopeBuffer &AudioManager::scope_buffer() const { return scope_; }

const audio::LoadMeter &AudioManager::load_meter() const { return load_; }

std::vector<ym2612::Note> AudioManager::sounding_notes() const {
  return {snapshot_.notes.begin(),
          snapshot_.notes.begin() +
              std::min<uint32_t>(snapshot_.note_count, 6)};
}

bool AudioManager::note_sounding(const ym2612::Note &note) const {
  const auto end =
      snapshot_.notes.begin() + std::min<uint32_t>(snapshot_.note_count, 6);
  return std::find(snapshot_.notes.begin(), end, note) != end;
}

VoiceActivityFrame AudioManager::voice_activity() const {
  return snapshot_.voices;
}

bool AudioManager::is_running() const { return running_; }

uint32_t AudioManager::sample_rate() const { return sample_rate_; }
