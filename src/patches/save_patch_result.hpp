#pragma once

#include <filesystem>
#include <string>

namespace patches {

struct SavePatchResult {
  enum class Status {
    Success,
    Duplicate,
    Error,
    Unsupported,
  };

  Status status = Status::Unsupported;
  std::filesystem::path path;
  std::string error_message;

  static SavePatchResult success(const std::filesystem::path &p) {
    return {Status::Success, p, ""};
  }
  static SavePatchResult duplicate() { return {Status::Duplicate, {}, ""}; }
  static SavePatchResult error(const std::string &msg) {
    return {Status::Error, {}, msg};
  }
  static SavePatchResult unsupported() { return {}; }
};

} // namespace patches
