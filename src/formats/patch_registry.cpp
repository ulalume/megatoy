#include "patch_registry.hpp"

#include "ctrmml.hpp"
#include "dmp.hpp"
#include "fui.hpp"
#include "gin.hpp"
#include "ginpkg.hpp"
#include "rym2612.hpp"
#include <algorithm>

namespace formats {

PatchRegistry &PatchRegistry::instance() {
  static PatchRegistry registry;
  return registry;
}

PatchRegistry::PatchRegistry() { register_defaults(); }

void PatchRegistry::register_format(const std::string &extension,
                                    PatchFormatHandler handler) {
  std::string ext_lower = extension;
  std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  handlers_[ext_lower] = std::move(handler);
}

const PatchFormatHandler *
PatchRegistry::handler_for_extension(const std::string &extension) const {
  std::string ext_lower = extension;
  std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  auto it = handlers_.find(ext_lower);
  if (it != handlers_.end()) {
    return &it->second;
  }
  return nullptr;
}

PatchLoadResult PatchRegistry::load(const std::filesystem::path &path) const {
  PatchLoadResult result;

  if (!std::filesystem::exists(path) ||
      !std::filesystem::is_regular_file(path)) {
    result.status = PatchLoadStatus::Failure;
    result.message = "File not found: " + path.string();
    return result;
  }

  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  auto handler = handler_for_extension(ext);
  if (!handler || !handler->read_file) {
    result.status = PatchLoadStatus::Failure;
    result.message = "Unsupported file extension: " + ext;
    return result;
  }

  auto patches = handler->read_file(path);
  const auto patches_size = patches.size();
  if (patches_size == 0) {
    result.status = PatchLoadStatus::Failure;
    if (ext == ".mml") {
      result.message = "No instruments found in MML file.";
    } else {
      result.message = "Failed to load " + handler->label + " patch.";
    }
  } else if (patches_size == 1) {
    if (patches[0].name.empty()) {
      patches[0].name = get_patch_name_from_file(path, ext);
    }
    result.status = PatchLoadStatus::Success;
    result.patches = std::move(patches);
  } else {
    for (auto &patch : patches) {
      if (patch.name.empty()) {
        patch.name = get_patch_name_from_file(path, ext);
      }
    }
    result.status = PatchLoadStatus::MultiInstrument;
    result.patches = std::move(patches);
  }
  return result;
}

bool PatchRegistry::write(const std::string &extension,
                          const ym2612::Patch &patch,
                          const std::filesystem::path &target) const {
  auto handler = handler_for_extension(extension);
  if (!handler) {
    return false;
  }
  if (handler->write_single) {
    return handler->write_single(patch, target);
  }
  return false;
}

bool PatchRegistry::write_text(const std::string &extension,
                               const ym2612::Patch &patch,
                               const std::filesystem::path &target) const {
  auto handler = handler_for_extension(extension);
  if (!handler) {
    return false;
  }
  if (handler->write_text) {
    return handler->write_text(patch, target);
  }
  return false;
}

std::optional<std::filesystem::path>
PatchRegistry::save_package(const std::string &extension,
                            const std::filesystem::path &dir,
                            const std::string &name,
                            const ym2612::Patch &patch) const {
  auto handler = handler_for_extension(extension);
  if (!handler || !handler->write_packaged) {
    return std::nullopt;
  }
  return handler->write_packaged(dir, patch, name);
}
void PatchRegistry::register_defaults() {
  register_format(".gin",
                  {formats::gin::read_file,
                   [](const std::filesystem::path &dir,
                      const ym2612::Patch &patch, const std::string &name) {
                     return formats::gin::save_patch(dir, patch, name);
                   },
                   nullptr, nullptr, "GIN"});
  register_format(".ginpkg",
                  {formats::ginpkg::read_file,
                   [](const std::filesystem::path &dir,
                      const ym2612::Patch &patch, const std::string &name) {
                     return formats::ginpkg::save_patch(dir, patch, name);
                   },
                   nullptr, nullptr, "GINPKG"});
  register_format(".dmp",
                  {formats::dmp::read_file,
                   nullptr,
                   formats::dmp::write_patch, nullptr, "DMP"});
  register_format(".rym2612",
                  {formats::rym2612::read_file, nullptr, nullptr, nullptr,
                   "RYM2612"});
  register_format(".fui",
                  {formats::fui::read_file, nullptr, nullptr, nullptr, "FUI"});
  register_format(".mml",
                  {formats::ctrmml::read_file, nullptr, nullptr,
                   formats::ctrmml::write_patch, "MML"});
}

} // namespace formats
