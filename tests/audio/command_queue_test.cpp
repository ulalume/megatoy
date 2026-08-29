// The chip has exactly one writer: the audio thread. These cover the handoff
// that makes that true, and the note state the UI reads without locking.

#include "audio/audio_command.hpp"
#include "audio/audio_engine.hpp"
#include "ym2612/note.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <atomic>
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

// With no audio thread draining, a MIDI submission must be dropped, not
// applied inline: that apply ran on the driver thread and raced the UI
// thread's own inline path above.
void test_midi_submissions_dropped_while_stopped() {
  AudioEngine engine;
  const auto note = ym2612::Note::from_midi_note(64);
  CHECK(!engine.is_running());
  CHECK(!engine.submit_from_midi(audio::AudioCommand::note_on(note, 100)));
  CHECK(!engine.notes().published_contains(note));

  CHECK(engine.initialize(kSampleRate));
  engine.shutdown();
  CHECK(!engine.is_running());
  CHECK(!engine.submit_from_midi(audio::AudioCommand::note_off(note)));
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

  // Keep the burst below the queue capacity so this test isolates the SPSC
  // handoff from the explicit overflow recovery below.
  constexpr int kRounds = 100;
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

void test_midi_overflow_cannot_leave_a_note_stuck() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  engine.submit(audio::AudioCommand::apply_patch(
      make_patch().global, make_patch().channel, make_patch().instrument));
  render_block(engine, 64);

  // The default MIDI queue holds 255 commands. Do not render while producing
  // this burst, guaranteeing that a note-off encounters a full queue.
  std::thread producer([&engine]() {
    for (int i = 0; i < 200; ++i) {
      const auto note = ym2612::Note::from_midi_note(60);
      engine.submit_from_midi(audio::AudioCommand::note_on(note, 100));
      CHECK(engine.submit_from_midi(audio::AudioCommand::note_off(note)));
    }
  });
  producer.join();

  render_block(engine, 64);
  CHECK(engine.notes().published_notes().empty());
}

// ------------------------------------------- what the envelope graph reads

const VoiceActivity *find_voice(const VoiceActivityFrame &frame,
                                uint8_t midi_note) {
  for (const VoiceActivity &voice : frame.voices) {
    if (voice.valid() && voice.midi_note == midi_note) {
      return &voice;
    }
  }
  return nullptr;
}

// The graph needs times, not just "is it sounding": when the key went down,
// when it came up, and where the chip's clock is now.
void test_the_voice_record_carries_the_key_times() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  engine.submit(audio::AudioCommand::apply_patch(
      make_patch().global, make_patch().channel, make_patch().instrument));
  render_block(engine, 64);
  CHECK(engine.rendered_samples() == 64);

  const auto note = ym2612::Note::from_midi_note(60);
  engine.submit(audio::AudioCommand::note_on(note, 100));
  render_block(engine, 64);
  CHECK(engine.rendered_samples() == 128);

  {
    const VoiceActivityFrame frame = engine.voice_activity();
    const VoiceActivity *voice = find_voice(frame, 60);
    CHECK(voice != nullptr);
    CHECK(voice->held);
    CHECK(voice->sequence == 1);
    // Commands are drained before a single frame of their block is rendered,
    // so the key-on lands at the position that block starts at.
    CHECK(voice->key_on_sample == 64);
    CHECK(frame.now_samples == 128);
    CHECK(frame.sample_rate == kSampleRate);
  }

  render_block(engine, 256);
  engine.submit(audio::AudioCommand::note_off(note));
  render_block(engine, 64);

  {
    const VoiceActivityFrame frame = engine.voice_activity();
    // The record OUTLIVES the note-off: the voice is releasing, and that is
    // what the cursor follows.
    CHECK(engine.notes().published_notes().empty());
    const VoiceActivity *voice = find_voice(frame, 60);
    CHECK(voice != nullptr);
    CHECK(!voice->held);
    CHECK(voice->sequence == 1);
    CHECK(voice->key_on_sample == 64);
    CHECK(voice->key_off_sample == 384);
  }
}

// A stolen channel must read as a new voice, not as the old one carrying on.
void test_a_steal_starts_a_new_voice() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  engine.submit(audio::AudioCommand::apply_patch(
      make_patch().global, make_patch().channel, make_patch().instrument));
  for (uint8_t i = 0; i < 6; ++i) {
    engine.submit(audio::AudioCommand::note_on(
        ym2612::Note::from_midi_note(static_cast<uint8_t>(60 + i)), 100));
  }
  render_block(engine, 64);

  const VoiceActivity *stolen = find_voice(engine.voice_activity(), 60);
  CHECK(stolen != nullptr);
  const uint64_t stolen_sequence = stolen->sequence;

  render_block(engine, 512);
  engine.submit(
      audio::AudioCommand::note_on(ym2612::Note::from_midi_note(72), 100));
  render_block(engine, 64);

  const VoiceActivityFrame frame = engine.voice_activity();
  // The oldest note is gone from its channel, replaced rather than released.
  CHECK(find_voice(frame, 60) == nullptr);
  const VoiceActivity *fresh = find_voice(frame, 72);
  CHECK(fresh != nullptr);
  CHECK(fresh->held);
  CHECK(fresh->sequence > stolen_sequence);
  CHECK(fresh->key_on_sample == 576);
}

// Six channels, six records, and the newest is the largest sequence -- which
// is the order the graph fades them in.
void test_every_channel_gets_its_own_record() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  engine.submit(audio::AudioCommand::apply_patch(
      make_patch().global, make_patch().channel, make_patch().instrument));
  uint64_t previous = 0;
  for (uint8_t i = 0; i < 6; ++i) {
    engine.submit(audio::AudioCommand::note_on(
        ym2612::Note::from_midi_note(static_cast<uint8_t>(60 + i)), 100));
    render_block(engine, 64);
    const VoiceActivity *voice = find_voice(engine.voice_activity(), 60 + i);
    CHECK(voice != nullptr);
    CHECK(voice->sequence > previous);
    previous = voice->sequence;
  }
  int valid = 0;
  for (const VoiceActivity &voice : engine.voice_activity().voices) {
    valid += voice.valid() ? 1 : 0;
  }
  CHECK(valid == 6);
}

/**
 * The seqlock, hammered.
 *
 * The audio thread rewrites the records while this thread reads them. Every
 * key-on plays a note derived from the sequence it will be given, so any
 * record whose note does not match its own sequence is a torn read -- one
 * voice's note paired with another's numbers, which is exactly the failure
 * the generation counter exists to prevent.
 */
void test_a_reader_never_sees_half_a_record() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  engine.submit(audio::AudioCommand::apply_patch(
      make_patch().global, make_patch().channel, make_patch().instrument));
  render_block(engine, 64);

  constexpr int kRounds = 4000;
  std::atomic<bool> done{false};
  std::thread audio_thread([&engine, &done]() {
    for (int i = 0; i < kRounds; ++i) {
      // note = 48 + (sequence - 1) % 24, where sequence is i + 1.
      const auto note =
          ym2612::Note::from_midi_note(static_cast<uint8_t>(48 + i % 24));
      engine.submit(audio::AudioCommand::note_on(note, 100));
      render_block(engine, 16);
      engine.submit(audio::AudioCommand::note_off(note));
      render_block(engine, 16);
    }
    done.store(true, std::memory_order_release);
  });

  uint64_t reads = 0;
  while (!done.load(std::memory_order_acquire)) {
    const VoiceActivityFrame frame = engine.voice_activity();
    for (const VoiceActivity &voice : frame.voices) {
      if (!voice.valid()) {
        continue;
      }
      const auto expected =
          static_cast<uint8_t>(48 + (voice.sequence - 1) % 24);
      CHECK(voice.midi_note == expected);
      CHECK(voice.key_on_sample <= frame.now_samples);
      if (!voice.held) {
        CHECK(voice.key_off_sample >= voice.key_on_sample);
        CHECK(voice.key_off_sample <= frame.now_samples);
      }
      ++reads;
    }
  }
  audio_thread.join();
  CHECK(reads > 0);
  std::cout << "seqlock: " << reads << " voice records read while writing\n";
}

} // namespace

int main() {
  test_queue_basics();
  test_commands_apply_on_render();
  test_applies_inline_when_stopped();
  test_midi_submissions_dropped_while_stopped();
  test_voice_limit_and_all_notes_off();
  test_midi_submissions_from_another_thread();
  test_midi_overflow_cannot_leave_a_note_stuck();
  test_the_voice_record_carries_the_key_times();
  test_a_steal_starts_a_new_voice();
  test_every_channel_gets_its_own_record();
  test_a_reader_never_sees_half_a_record();

  std::cout << "All audio command tests passed\n";
  return 0;
}
