#include "patch_editing_session.hpp"

#include "audio/audio_manager.hpp"
#include "formats/dmp.hpp"
#include "preferences/preference_manager.hpp"
#include "ym2612/channel.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

using megatoy::system::DirectoryService;

PatchEditingSession::PatchEditingSession(DirectoryService &directories,
                                         AudioManager &audio)
    : directories_(directories), audio_(audio), patch_manager_(directories_),
      channel_allocator_() {}

ym2612::Patch &PatchEditingSession::current_patch() {
  return patch_manager_.current_patch();
}

const ym2612::Patch &PatchEditingSession::current_patch() const {
  return patch_manager_.current_patch();
}

patches::PatchManager &PatchEditingSession::patch_manager() {
  return patch_manager_;
}

const patches::PatchManager &PatchEditingSession::patch_manager() const {
  return patch_manager_;
}

patches::PatchRepository &PatchEditingSession::repository() {
  return patch_manager_.repository();
}

const patches::PatchRepository &PatchEditingSession::repository() const {
  return patch_manager_.repository();
}

ChannelAllocator &PatchEditingSession::channel_allocator() {
  return channel_allocator_;
}

const ChannelAllocator &PatchEditingSession::channel_allocator() const {
  return channel_allocator_;
}

void PatchEditingSession::initialize_patch_defaults() {
  const auto &paths = directories_.paths();
  const auto init_patch_path = paths.builtin_presets_root / "init.dmp";
  auto &patch = patch_manager_.current_patch();

  bool loaded_builtin = false;
  if (!paths.builtin_presets_root.empty() &&
      std::filesystem::exists(init_patch_path)) {
    const auto patches = formats::dmp::read_file(init_patch_path);
    if (!patches.empty()) {
      patch_manager_.set_current_patch_path(init_patch_path);
      patch = std::move(patches[0]);
      loaded_builtin = true;
    } else {
      std::cerr << "Failed to load builtin init patch: " << init_patch_path
                << "\n";
    }
  }

  if (!loaded_builtin) {
    patch.name = "init";
    patch.global = {
        .dac_enable = false,
        .lfo_enable = false,
        .lfo_frequency = 0,
    };

    patch.channel = {
        .left_speaker = true,
        .right_speaker = true,
        .amplitude_modulation_sensitivity = 0,
        .frequency_modulation_sensitivity = 0,
    };

    patch.instrument = {
        .feedback = 7,
        .algorithm = 3,
        .operators =
            {
                {31, 0, 0, 5, 0, 48, 0, 1, 3, 0, false, false},
                {31, 0, 0, 5, 0, 24, 0, 1, 1, 0, false, false},
                {31, 0, 0, 5, 0, 36, 0, 1, 2, 0, false, false},
                {31, 0, 0, 5, 0, 12, 0, 1, 4, 0, false, false},
            },
    };
    patch_manager_.set_current_patch_path({});
  }
}

void PatchEditingSession::refresh_directories() {
  patch_manager_.refresh_directories();
}

bool PatchEditingSession::load_patch_no_history(
    const patches::PatchEntry &entry) {
  if (!patch_manager_.load_patch(entry)) {
    return false;
  }
  apply_patch_to_audio();
  return true;
}

void PatchEditingSession::set_current_patch(
    const ym2612::Patch &patch, const std::filesystem::path &source_path) {
  patch_manager_.current_patch() = patch;
  patch_manager_.set_current_patch_path(source_path);
  apply_patch_to_audio();
}

void PatchEditingSession::apply_patch_to_audio() {
  audio_.apply_patch_to_all_channels(patch_manager_.current_patch());
}

patches::PatchDropResult
PatchEditingSession::load_patch_from_path(const std::filesystem::path &path) {
  return patches::load_patch_from_path(path);
}

bool PatchEditingSession::note_on(
    ym2612::Note note, uint8_t velocity,
    const PreferenceManager::UIPreferences &prefs) {
  const uint8_t clamped_velocity =
      std::min<uint8_t>(velocity, static_cast<uint8_t>(127));
  const uint8_t effective_velocity =
      prefs.use_velocity ? clamped_velocity : static_cast<uint8_t>(127);

  auto claim =
      channel_allocator_.note_on(note, prefs.steal_oldest_note_when_full);
  if (!claim) {
    return false;
  }

  if (claim->replaced_note) {
    audio_.device().channel(claim->channel).write_key_off();
  }

  auto ym_channel = audio_.device().channel(claim->channel);
  ym_channel.write_frequency(note);
  auto instrument =
      patch_manager_.current_patch().instrument.clone_with_velocity(
          effective_velocity);
  ym_channel.write_instrument(instrument);
  ym_channel.write_key_on(
      instrument.operators[0].enable, instrument.operators[1].enable,
      instrument.operators[2].enable, instrument.operators[3].enable);
  return true;
}

bool PatchEditingSession::note_off(ym2612::Note note) {
  return channel_allocator_.note_off(note, audio_.device());
}

bool PatchEditingSession::note_is_active(const ym2612::Note &note) const {
  return channel_allocator_.is_note_active(note);
}

void PatchEditingSession::release_all_notes() {
  channel_allocator_.release_all(audio_.device());
}

const std::array<bool, 6> &PatchEditingSession::active_channels() const {
  return channel_allocator_.channel_usage();
}

PatchEditingSession::PatchSnapshot
PatchEditingSession::capture_snapshot() const {
  PatchSnapshot snapshot;
  snapshot.patch = patch_manager_.current_patch();
  snapshot.path = patch_manager_.current_patch_path();
  return snapshot;
}

void PatchEditingSession::restore_snapshot(const PatchSnapshot &snapshot) {
  patch_manager_.current_patch() = snapshot.patch;
  if (snapshot.path.empty()) {
    patch_manager_.set_current_patch_path({});
  } else {
    patch_manager_.set_current_patch_path(snapshot.path);
  }
  apply_patch_to_audio();
}
