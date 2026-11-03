#include "../patches/session_test_environment.hpp"
#include "audio/audio_transport.hpp"
#include "audio/load_meter.hpp"
#include "ym2612/note.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

using session_tests::LaunchedEnvironment;

class TestAudioTransport final : public AudioTransport {
public:
  bool start(std::uint32_t, RenderCallback callback) override {
    if (failed_starts_ < starts_to_fail_) {
      ++failed_starts_;
      return false;
    }
    callback_ = std::move(callback);
    active_ = true;
    ++start_count_;
    return true;
  }

  void stop() override {
    active_ = false;
    callback_ = {};
  }

  bool is_active() const override { return active_; }

  void set_buffer_frames(int frames) override { buffer_frames_ = frames; }
  int buffer_frames() const { return buffer_frames_; }
  int start_count() const { return start_count_; }

  /// Refuse the next `count` calls to start(), as a device that will not open
  /// at the requested size does.
  void refuse_next_starts(int count) {
    starts_to_fail_ = count;
    failed_starts_ = 0;
  }

  float render_ac_peak(uint32_t frames) {
    CHECK(callback_);
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2, 0);
    const uint32_t bytes = frames * 2 * static_cast<uint32_t>(sizeof(int16_t));
    CHECK(callback_(bytes, pcm.data()) == bytes);

    double sum = 0.0;
    for (int16_t sample : pcm) {
      sum += sample;
    }
    const double mean = sum / static_cast<double>(pcm.size());

    double peak = 0.0;
    for (int16_t sample : pcm) {
      peak = std::max(peak, std::abs(static_cast<double>(sample) - mean));
    }
    return static_cast<float>(peak / 32768.0);
  }

private:
  RenderCallback callback_;
  bool active_ = false;
  int buffer_frames_ = 0;
  int start_count_ = 0;
  int starts_to_fail_ = 0;
  int failed_starts_ = 0;
};

ym2612::Patch make_session_audio_patch(uint8_t total_level) {
  ym2612::Patch patch;
  patch.name = "session audio";
  patch.instrument.algorithm = 7;
  for (auto &settings : patch.instrument.operators) {
    settings.attack_rate = 31;
    settings.release_rate = 15;
    settings.total_level = total_level;
    settings.multiple = 1;
  }
  return patch;
}

void test_session_applies_direct_patch_mutation() {
  LaunchedEnvironment env("session_audio_test");
  auto transport = std::make_unique<TestAudioTransport>();
  auto *test_transport = transport.get();
  AudioManager audio(std::move(transport));
  patches::PatchSession session(env.directories, env.preferences, audio);
  CHECK(audio.initialize(44100));

  session.current_patch() = make_session_audio_patch(20);
  CHECK(session.apply_patch_to_audio_if_changed());
  CHECK(!session.apply_patch_to_audio_if_changed());

  PreferenceManager::UIPreferences prefs{};
  prefs.use_velocity = true;
  prefs.steal_oldest_note_when_full = true;
  CHECK(session.note_on(ym2612::Note::from_midi_note(60), 127, prefs));
  const float before = test_transport->render_ac_peak(4410);

  session.current_patch().instrument.operators[0].total_level = 110;
  CHECK(session.apply_patch_to_audio_if_changed());
  const float after = test_transport->render_ac_peak(4410);
  CHECK(after < before * 0.9f);
  CHECK(!session.apply_patch_to_audio_if_changed());

  session.current_patch().name = "name only";
  CHECK(!session.apply_patch_to_audio_if_changed());
}

void test_performance_commands_do_not_dirty_session() {
  LaunchedEnvironment env("session_audio_test");
  auto transport = std::make_unique<TestAudioTransport>();
  auto *test_transport = transport.get();
  AudioManager audio(std::move(transport));
  patches::PatchSession session(env.directories, env.preferences, audio);
  CHECK(audio.initialize(44100));

  session.current_patch() = make_session_audio_patch(20);
  session.mark_as_clean();
  session.apply_patch_to_audio();
  test_transport->render_ac_peak(64);
  CHECK(!session.is_modified());
  CHECK(!session.apply_patch_to_audio_if_changed());
  const auto patch_before = session.current_patch();

  CHECK(audio.submit(audio::AudioCommand::pitch_bend(12288)));
  CHECK(audio.submit(audio::AudioCommand::mod_wheel(127)));
  CHECK(audio.submit(audio::AudioCommand::mod_wheel(0)));
  test_transport->render_ac_peak(64);

  CHECK(session.current_patch() == patch_before);
  CHECK(!session.is_modified());
  CHECK(!session.apply_patch_to_audio_if_changed());
}

// A buffer size change reopens the device while the app runs. The patch has
// to survive it, nothing may be left keyed on, and the load history cannot
// carry entries recorded at the previous size.
void test_buffer_size_change_reopens_the_device() {
  LaunchedEnvironment env("session_audio_test");
  auto transport = std::make_unique<TestAudioTransport>();
  auto *test_transport = transport.get();
  AudioManager audio(std::move(transport));
  patches::PatchSession session(env.directories, env.preferences, audio);
  CHECK(audio.initialize(44100, 512));
  CHECK(audio.buffer_frames() == 512);
  CHECK(test_transport->buffer_frames() == 512);
  CHECK(test_transport->start_count() == 1);

  session.current_patch() = make_session_audio_patch(20);
  session.apply_patch_to_audio();

  PreferenceManager::UIPreferences prefs{};
  prefs.use_velocity = true;
  prefs.steal_oldest_note_when_full = true;
  const auto c4 = ym2612::Note::from_midi_note(60);
  CHECK(session.note_on(c4, 127, prefs));
  const float sounding = test_transport->render_ac_peak(4410);
  CHECK(sounding > 0.0f);
  CHECK(audio.notes().published_contains(c4));
  CHECK(audio.load_meter().history().count > 0);

  // Nothing reopens while the frames the preference resolves to stay put.
  CHECK(audio.set_buffer_frames(512));
  CHECK(test_transport->start_count() == 1);

  CHECK(audio.set_buffer_frames(1024));
  CHECK(test_transport->start_count() == 2);
  CHECK(test_transport->buffer_frames() == 1024);
  CHECK(audio.buffer_frames() == 1024);
  CHECK(audio.is_running());

  // The note the reopen interrupted is released, not left sounding.
  CHECK(!audio.notes().published_contains(c4));
  CHECK(audio.notes().published_notes().empty());
  CHECK(audio.load_meter().history().count == 0);

  // The patch came across, so the same note plays at the same level.
  CHECK(session.note_on(c4, 127, prefs));
  const float after = test_transport->render_ac_peak(4410);
  CHECK(after > sounding * 0.5f);
  audio.submit(audio::AudioCommand::all_notes_off());

  // A device that will not open at the new size keeps the one that works.
  test_transport->refuse_next_starts(1);
  CHECK(!audio.set_buffer_frames(2048));
  CHECK(audio.is_running());
  CHECK(audio.buffer_frames() == 1024);
  CHECK(test_transport->buffer_frames() == 1024);
  CHECK(session.note_on(c4, 127, prefs));
  CHECK(test_transport->render_ac_peak(4410) > sounding * 0.5f);
  audio.submit(audio::AudioCommand::all_notes_off());
}

void test_note_allocation() {
  LaunchedEnvironment env("session_audio_test");
  PreferenceManager::UIPreferences prefs{};
  prefs.use_velocity = true;
  prefs.steal_oldest_note_when_full = true;

  ym2612::Note c4 = ym2612::Note::from_midi_note(60);
  ym2612::Note e4 = ym2612::Note::from_midi_note(64);

  CHECK(env.session.note_on(c4, 90, prefs));
  CHECK(env.audio.notes().published_contains(c4));
  CHECK(env.session.note_on(e4, 70, prefs));
  CHECK(env.audio.notes().published_contains(e4));

  CHECK(env.audio.submit(audio::AudioCommand::note_off(c4)));
  CHECK(!env.audio.notes().published_contains(c4));
  env.audio.submit(audio::AudioCommand::all_notes_off());
  CHECK(env.audio.notes().published_notes().empty());
}

} // namespace

int main() {
  test_session_applies_direct_patch_mutation();
  test_performance_commands_do_not_dirty_session();
  test_buffer_size_change_reopens_the_device();
  test_note_allocation();
  std::cout << "All session audio tests passed\n";
  return 0;
}
