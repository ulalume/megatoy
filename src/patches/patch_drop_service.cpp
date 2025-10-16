#include "patch_drop_service.hpp"

#include "parsers/patch_loader.hpp"

#include <filesystem>

namespace patches {

PatchDropResult load_patch_from_path(const std::filesystem::path &path) {
  PatchDropResult result;
  result.source_path = path;

  if (!std::filesystem::exists(path) ||
      !std::filesystem::is_regular_file(path)) {
    result.status = PatchDropResult::Status::Error;
    result.error_message = "File not found: " + path.string();
    return result;
  }

  const auto parsed = parsers::load_patch_from_file(path);
  switch (parsed.status) {
  case parsers::PatchLoadStatus::Success:
    result.status = PatchDropResult::Status::Loaded;
    result.patch = parsed.patch;
    result.history_label = "Load Patch: " + path.filename().string();
    return result;
  case parsers::PatchLoadStatus::MultiInstrument:
    result.status = PatchDropResult::Status::MultiInstrument;
    result.instruments = parsed.instruments;
    return result;
  case parsers::PatchLoadStatus::Failure:
  default:
    result.status = PatchDropResult::Status::Error;
    result.error_message =
        parsed.message.empty()
            ? "Unsupported file format: " + path.filename().string()
            : parsed.message;
    return result;
  }
}

} // namespace patches
