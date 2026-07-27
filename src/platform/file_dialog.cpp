#include "file_dialog.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

#if !defined(__EMSCRIPTEN__)
#include <nfd.h>
#endif

namespace platform::file_dialog {

#if defined(__EMSCRIPTEN__)

// The browser has no native dialog to call; downloads and localStorage cover
// saving on the web build.

bool initialize() { return true; }
void shutdown() {}

DialogResult pick_folder(const std::filesystem::path &,
                         std::filesystem::path &) {
  return DialogResult::Cancelled;
}

DialogResult save_file(const std::filesystem::path &,
                       const std::string &default_name,
                       const std::vector<FileFilter> &,
                       std::filesystem::path &selected_path) {
  selected_path = std::filesystem::path(default_name);
  return DialogResult::Cancelled;
}

#else

namespace {

bool g_nfd_initialized = false;

// NFD speaks UTF-8 on every platform. Converting through std::u8string keeps
// it that way; building a path straight from a narrow char* would decode with
// the native narrow encoding (the ANSI codepage on Windows) and mangle any
// non-ASCII path.
std::filesystem::path path_from_utf8(const nfdu8char_t *value) {
  const std::u8string utf8(reinterpret_cast<const char8_t *>(value));
  return std::filesystem::path(utf8);
}

std::string path_to_utf8(const std::filesystem::path &path) {
  const std::u8string utf8 = path.u8string();
  return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
}

struct FilterItems {
  std::vector<std::string> labels;
  std::vector<std::string> specs;
  std::vector<nfdu8filteritem_t> items;
};

FilterItems build_filter_items(const std::vector<FileFilter> &filters) {
  FilterItems storage;
  if (filters.empty()) {
    return storage;
  }

  storage.labels.reserve(filters.size());
  storage.specs.reserve(filters.size());
  storage.items.reserve(filters.size());

  for (const auto &filter : filters) {
    std::ostringstream spec_stream;
    bool first = true;
    for (const auto &extension : filter.extensions) {
      if (!first) {
        spec_stream << ',';
      }
      first = false;
      spec_stream << extension;
    }

    storage.labels.push_back(filter.label);
    storage.specs.push_back(spec_stream.str());

    const nfdu8char_t *name_ptr =
        storage.labels.back().empty() ? nullptr : storage.labels.back().c_str();
    const nfdu8char_t *spec_ptr =
        storage.specs.back().empty() ? nullptr : storage.specs.back().c_str();
    storage.items.push_back({name_ptr, spec_ptr});
  }

  return storage;
}

void report_error(const char *context) {
  const char *error = NFD_GetError();
  std::cerr << context << ": " << (error != nullptr ? error : "unknown error")
            << std::endl;
}

} // namespace

bool initialize() {
  if (g_nfd_initialized) {
    return true;
  }
  if (NFD_Init() == NFD_OKAY) {
    g_nfd_initialized = true;
    return true;
  }
  report_error("Failed to initialize Native File Dialog");
  return false;
}

void shutdown() {
  if (g_nfd_initialized) {
    NFD_Quit();
    g_nfd_initialized = false;
  }
}

DialogResult pick_folder(const std::filesystem::path &default_path,
                         std::filesystem::path &selected_path) {
  if (!initialize()) {
    return DialogResult::Error;
  }

  const std::string default_utf8 =
      default_path.empty() ? std::string() : path_to_utf8(default_path);

  nfdu8char_t *out_path = nullptr;
  const nfdresult_t result = NFD_PickFolderU8(
      &out_path, default_utf8.empty() ? nullptr : default_utf8.c_str());

  if (result == NFD_OKAY) {
    selected_path = path_from_utf8(out_path);
    NFD_FreePathU8(out_path);
    return DialogResult::Ok;
  }
  if (result == NFD_CANCEL) {
    return DialogResult::Cancelled;
  }
  report_error("Error selecting directory");
  return DialogResult::Error;
}

DialogResult save_file(const std::filesystem::path &default_dir,
                       const std::string &default_name,
                       const std::vector<FileFilter> &filters,
                       std::filesystem::path &selected_path) {
  if (!initialize()) {
    return DialogResult::Error;
  }

  FilterItems filter_items = build_filter_items(filters);
  const std::string default_utf8 =
      default_dir.empty() ? std::string() : path_to_utf8(default_dir);

  const nfdu8filteritem_t *filter_ptr =
      filter_items.items.empty() ? nullptr : filter_items.items.data();
  const nfdfiltersize_t filter_count =
      static_cast<nfdfiltersize_t>(filter_items.items.size());

  nfdu8char_t *out_path = nullptr;
  const nfdresult_t result =
      NFD_SaveDialogU8(&out_path, filter_ptr, filter_count,
                       default_utf8.empty() ? nullptr : default_utf8.c_str(),
                       default_name.empty() ? nullptr : default_name.c_str());

  if (result == NFD_OKAY) {
    selected_path = path_from_utf8(out_path);
    NFD_FreePathU8(out_path);
    return DialogResult::Ok;
  }
  if (result == NFD_CANCEL) {
    return DialogResult::Cancelled;
  }
  report_error("Error selecting file");
  return DialogResult::Error;
}

#endif // __EMSCRIPTEN__

} // namespace platform::file_dialog
