// The chip has exactly one writer: the audio thread. These cover the handoff
// that makes that true, and the note state the UI reads without locking.

#include "audio/audio_command.hpp"
#include "audio/audio_engine.hpp"
#include "ym2612/note.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 44100;

ym2612::Patch make_patch() {
  ym2612::Patch patch;
  patch.instrument.algorithm = 7;
  for (auto &op : patch.instrument.operators) {
    op.attack_rate = 31;
    op.release_rate = 15;
    op.total_level = 20;
    op.multiple = 1;
  }
  return patch;
}

void render_block(AudioEngine &engine, uint32_t frames) {
  std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2, 0);
  engine.render(frames * 2 * static_cast<uint32_t>(sizeof(int16_t)),
                pcm.data());
}

void test_queue_basics() {
  audio::AudioCommandQueue queue(4);
  audio::AudioCommand out;

  CHECK(queue.empty());
  CHECK(!queue.pop(out));

  const auto note = ym2612::Note::from_midi_note(60);
  CHECK(queue.push(audio::AudioCommand::note_on(note, 100)));
  CHECK(!queue.empty());
  CHECK(queue.pop(out));
  CHECK(out.type == audio::AudioCommand::Type::NoteOn);
  CHECK(out.note == note);
  CHECK(out.velocity == 100);
  CHECK(queue.empty());

  // Capacity 4 holds three before reporting full, and refuses rather than
  // overwriting an entry the consumer has not taken yet.
  CHECK(queue.push(audio::AudioCommand::all_notes_off()));
  CHECK(queue.push(audio::AudioCommand::all_notes_off()));
  CHECK(queue.push(audio::AudioCommand::all_notes_off()));
  CHECK(!queue.push(audio::AudioCommand::all_notes_off()));
}

// A submitted note must not reach the chip until the audio thread renders --
// that deferral is the whole point.
void test_commands_apply_on_render() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));

  engine.submit(audio::AudioCommand::apply_patch(
      make_patch().global, make_patch().channel, make_patch().instrument));
  render_block(engine, 64);

  const auto note = ym2612::Note::from_midi_note(60);
  engine.submit(audio::AudioCommand::note_on(note, 100));

  // Still queued: nothing has rendered since.
  CHECK(engine.notes().published_notes().empty());

  render_block(engine, 64);
  CHECK(engine.notes().published_contains(note));
  CHECK(engine.notes().published_notes().size() == 1);
  CHECK(engine.notes().published_channels()[0]);

  engine.submit(audio::AudioCommand::note_off(note));
  render_block(engine, 64);
  CHECK(!engine.notes().published_contains(note));
  CHECK(engine.notes().published_notes().empty());
}

// With no device open there is no audio thread to defer to, so state has to
// stay consistent anyway.
void test_applies_inline_when_stopped() {
  AudioEngine engine;
  const auto note = ym2612::Note::from_midi_note(64);
  engine.submit(audio::AudioCommand::note_on(note, 100));
  CHECK(engine.notes().published_contains(note));
}

void test_voice_limit_and_all_notes_off() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  engine.submit(audio::AudioCommand::apply_patch(
      make_patch().global, make_patch().channel, make_patch().instrument));

  for (uint8_t i = 0; i < 8; ++i) {
    engine.submit(audio::AudioCommand::note_on(
        ym2612::Note::from_midi_note(static_cast<uint8_t>(60 + i)), 100));
  }
  render_block(engine, 64);
  // Six channels, so six notes sound however many arrive.
  CHECK(engine.notes().published_notes().size() == 6);

  engine.submit(audio::AudioCommand::all_notes_off());
  render_block(engine, 64);
  CHECK(engine.notes().published_notes().empty());
}

// The MIDI queue has its own producer. Pushing from another thread while the
// "audio thread" renders must not lose or corrupt anything.
void test_midi_submissions_from_another_thread() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  engine.submit(audio::AudioCommand::apply_patch(
      make_patch().global, make_patch().channel, make_patch().instrument));
  render_block(engine, 64);

  constexpr int kRounds = 200;
  std::thread producer([&engine]() {
    for (int i = 0; i < kRounds; ++i) {
      const auto note = ym2612::Note::from_midi_note(60);
      engine.submit_from_midi(audio::AudioCommand::note_on(note, 100));
      engine.submit_from_midi(audio::AudioCommand::note_off(note));
    }
  });

  for (int i = 0; i < kRounds * 4; ++i) {
    render_block(engine, 16);
  }
  producer.join();

  // Drain whatever the producer left behind.
  for (int i = 0; i < 16; ++i) {
    render_block(engine, 16);
  }
  // Every note-on was paired with a note-off, so nothing may be left sounding.
  CHECK(engine.notes().published_notes().empty());
}

} // namespace

int main() {
  test_queue_basics();
  test_commands_apply_on_render();
  test_applies_inline_when_stopped();
  test_voice_limit_and_all_notes_off();
  test_midi_submissions_from_another_thread();

  std::cout << "All audio command tests passed\n";
  return 0;
}
