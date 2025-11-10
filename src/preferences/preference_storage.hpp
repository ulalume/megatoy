#pragma once

#include "platform/virtual_file_system.hpp"
#include "preferences_data.hpp"

#include <filesystem>
#include <memory>

class PreferenceStorage {
public:
  virtual ~PreferenceStorage() = default;

  virtual bool load(PreferenceData &data) = 0;
  virtual bool save(const PreferenceData &data) = 0;
};

std::unique_ptr<PreferenceStorage>
make_json_preference_storage(const std::filesystem::path &path,
                             platform::VirtualFileSystem &vfs);
