#include "patch_registry.hpp"

#include "ginpkg.hpp"
#include "ym2612_format_adapter.hpp"
#include <algorithm>
#include <fstream>

namespace formats {

PatchRegistry &PatchRegistry::instance() {
  static PatchRegistry registry;
  return registry;
}

PatchRegistry::PatchRegistry() { register_defaults(); }

void PatchRegistry::register_format(const std::string &extension,
                                    PatchFormatHandler handler) {
  std::string ext_lower = extension;
  std::transform(
      ext_lower.begin(), ext_lower.end(), ext_lower.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  handlers_[ext_lower] = std::move(handler);
}

const PatchFormatHandler *
PatchRegistry::handler_for_extension(const std::string &extension) const {
  std::string ext_lower = extension;
  std::transform(
      ext_lower.begin(), ext_lower.end(), ext_lower.begin(),
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
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

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

std::optional<std::filesystem::path> PatchRegistry::save_package(
    const std::string &extension, const std::filesystem::path &dir,
    const std::string &name, const ym2612::Patch &patch) const {
  auto handler = handler_for_extension(extension);
  if (!handler || !handler->write_packaged) {
    return std::nullopt;
  }
  return handler->write_packaged(dir, patch, name);
}

std::vector<ExportFormatInfo> PatchRegistry::export_formats() const {
  std::vector<ExportFormatInfo> formats;
  formats.reserve(handlers_.size());
  for (const auto &[ext, handler] : handlers_) {
    if (handler.write_single) {
      ExportFormatInfo info;
      info.extension = ext;
      info.label = handler.label.empty() ? ext : handler.label;
      info.is_text = false;
      formats.push_back(std::move(info));
    }
    if (handler.write_text) {
      ExportFormatInfo info;
      info.extension = ext;
      info.label = handler.label.empty() ? ext : handler.label;
      info.is_text = true;
      formats.push_back(std::move(info));
    }
  }
  std::sort(formats.begin(), formats.end(),
            [](const ExportFormatInfo &a, const ExportFormatInfo &b) {
              return a.label < b.label;
            });
  return formats;
}
void PatchRegistry::register_defaults() {
  // Everything ym2612_format supports is registered from its own table, so a
  // format added upstream shows up here without further changes.
  for (const auto &info : adapter::known_formats()) {
    const auto format = info.format;

    // .ginpkg stays megatoy's own: it is a versioned container with a patch
    // history, which the library only reads.
    if (format == ym2612_format::Format::Ginpkg) {
      continue;
    }

    PatchFormatHandler handler;
    handler.label = info.name;

    if (info.can_read) {
      handler.read_file = [format](const std::filesystem::path &path) {
        return adapter::read_file(format, path);
      };
    }

    if (info.can_write && info.is_text) {
      handler.write_text = [format](const ym2612::Patch &patch,
                                    const std::filesystem::path &target) {
        auto text = adapter::serialize_text(format, patch);
        if (!text) {
          return false;
        }
        std::ofstream file(target);
        if (!file) {
          return false;
        }
        file << *text;
        return static_cast<bool>(file);
      };
    } else if (info.can_write) {
      handler.write_single = [format](const ym2612::Patch &patch,
                                      const std::filesystem::path &target) {
        return adapter::write_file(format, patch, target);
      };
    }

    // .gin is megatoy's plain save format, so it also gets a packaged writer
    // that names the file from the patch.
    if (format == ym2612_format::Format::Gin) {
      handler.write_packaged =
          [format](
              const std::filesystem::path &dir, const ym2612::Patch &patch,
              const std::string &name) -> std::optional<std::filesystem::path> {
        const auto path = dir / (name + ".gin");
        return adapter::write_file(format, patch, path)
                   ? std::optional<std::filesystem::path>(path)
                   : std::nullopt;
      };
    }

    register_format(adapter::extension_for(format), std::move(handler));
  }

  register_format(".ginpkg",
                  {formats::ginpkg::read_file,
                   [](const std::filesystem::path &dir,
                      const ym2612::Patch &patch, const std::string &name) {
                     return formats::ginpkg::save_patch(dir, patch, name);
                   },
                   nullptr, nullptr, "GINPKG"});
}

} // namespace formats
