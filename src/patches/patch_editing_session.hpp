#pragma once

#include "channel_allocator.hpp"
#include "patches/patch_drop_service.hpp"
#include "patches/patch_manager.hpp"
#include "preferences/preference_manager.hpp"
#include "system/directory_service.hpp"
#include "ym2612/note.hpp"
#include <array>
#include <filesystem>
#include <string>

class AudioSubsystem;
class PatchEditingSession {
public:
  PatchEditingSession(megatoy::system::DirectoryService &directories,
                      AudioSubsystem &audio);

  ym2612::Patch &current_patch();
  const ym2612::Patch &current_patch() const;

  patches::PatchManager &patch_manager();
  const patches::PatchManager &patch_manager() const;
  patches::PatchRepository &repository();
  const patches::PatchRepository &repository() const;

  ChannelAllocator &channel_allocator();
  const ChannelAllocator &channel_allocator() const;

  void initialize_patch_defaults();
  void refresh_directories();

  bool load_patch_no_history(const patches::PatchEntry &entry);
  void set_current_patch(const ym2612::Patch &patch,
                         const std::filesystem::path &source_path);
  void apply_patch_to_audio();

  patches::PatchDropResult load_patch_from_path(const std::filesystem::path &path);

  bool note_on(ym2612::Note note, uint8_t velocity,
               const PreferenceManager::UIPreferences &prefs);
  bool note_off(ym2612::Note note);
  bool note_is_active(const ym2612::Note &note) const;
  void release_all_notes();
  const std::array<bool, 6> &active_channels() const;

  struct PatchSnapshot {
    ym2612::Patch patch;
    std::string path;
    bool operator==(const PatchSnapshot &other) const {
      return patch == other.patch && path == other.path;
    }
  };

  PatchSnapshot capture_snapshot() const;
  void restore_snapshot(const PatchSnapshot &snapshot);

private:
  megatoy::system::DirectoryService &directories_;
  AudioSubsystem &audio_;
  patches::PatchManager patch_manager_;
  ChannelAllocator channel_allocator_;
};
