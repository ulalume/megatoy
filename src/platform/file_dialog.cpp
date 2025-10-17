#include "file_dialog.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

#ifdef __APPLE__
#include <cstdio>
#else
#include <nfd.h>
#endif

namespace platform::file_dialog {

namespace {

#ifdef __APPLE__
std::string escape_for_applescript(const std::string &input) {
  std::string escaped;
  escaped.reserve(input.size() * 2);
  for (char c : input) {
    if (c == '\\' || c == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  return escaped;
}

std::string run_applescript(const std::string &script) {
  std::string command =
      "osascript 2>/dev/null <<'APPLESCRIPT'\n" + script + "APPLESCRIPT\n";

  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) {
    std::cerr << "Failed to invoke osascript" << std::endl;
    return {};
  }

  std::string output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }
  int status = pclose(pipe);
  if (status != 0) {
    return {};
  }

  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
    output.pop_back();
  }
  return output;
}
#else
bool g_nfd_initialized = false;

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
    for (const auto &ext : filter.extensions) {
      if (!first) {
        spec_stream << ',';
      }
      first = false;
      spec_stream << ext;
    }

    storage.labels.push_back(filter.label);
    storage.specs.push_back(spec_stream.str());
    const nfdu8char_t *name_ptr = nullptr;
    if (!storage.labels.back().empty()) {
      name_ptr = storage.labels.back().c_str();
    }
    const nfdu8char_t *spec_ptr = nullptr;
    if (!storage.specs.back().empty()) {
      spec_ptr = storage.specs.back().c_str();
    }
    storage.items.push_back({name_ptr, spec_ptr});
  }

  return storage;
}
#endif

} // namespace

bool initialize() {
#ifdef __APPLE__
  return true;
#else
  if (g_nfd_initialized) {
    return true;
  }
  if (NFD_Init() == NFD_OKAY) {
    g_nfd_initialized = true;
    return true;
  }
  const char *error = NFD_GetError();
  if (error != nullptr) {
    std::cerr << "Failed to initialize Native File Dialog: " << error
              << std::endl;
  } else {
    std::cerr << "Failed to initialize Native File Dialog" << std::endl;
  }
  return false;
#endif
}

void shutdown() {
#ifndef __APPLE__
  if (g_nfd_initialized) {
    NFD_Quit();
    g_nfd_initialized = false;
  }
#endif
}

DialogResult pick_folder(const std::filesystem::path &default_path,
                         std::filesystem::path &selected_path) {
#ifdef __APPLE__
  std::filesystem::path base = default_path;
  if (base.empty()) {
    const char *home = std::getenv("HOME");
    base = home ? std::filesystem::path(home) : std::filesystem::path("/");
  }

  std::string script = "set defaultPath to POSIX file \"" +
                       escape_for_applescript(base.string()) + "\"\n";
  script +=
      "set chosenFolder to choose folder with prompt \"Select directory\" "
      "default location defaultPath\n";
  script += "POSIX path of chosenFolder\n";

  std::string output = run_applescript(script);
  if (output.empty()) {
    return DialogResult::Cancelled;
  }
  selected_path = std::filesystem::path(output);
  return DialogResult::Ok;
#else
  if (!initialize()) {
    return DialogResult::Error;
  }

  nfdu8char_t *out_path = nullptr;
  const std::string default_utf8 =
      default_path.empty() ? std::string() : default_path.string();
  nfdresult_t result = NFD_PickFolderU8(
      &out_path, default_utf8.empty() ? nullptr : default_utf8.c_str());
  if (result == NFD_OKAY) {
    selected_path = std::filesystem::path(out_path);
    NFD_FreePathU8(out_path);
    return DialogResult::Ok;
  }
  if (result == NFD_CANCEL) {
    return DialogResult::Cancelled;
  }
  const char *error = NFD_GetError();
  if (error != nullptr) {
    std::cerr << "Error selecting directory: " << error << std::endl;
  }
  return DialogResult::Error;
#endif
}

DialogResult save_file(const std::filesystem::path &default_dir,
                       const std::string &default_name,
                       const std::vector<FileFilter> &filters,
                       std::filesystem::path &selected_path) {
#ifdef __APPLE__
  std::filesystem::path base = default_dir;
  if (base.empty()) {
    const char *home = std::getenv("HOME");
    base = home ? std::filesystem::path(home) : std::filesystem::path("/");
  }

  std::string script = "set defaultPath to POSIX file \"" +
                       escape_for_applescript(base.string()) + "\"\n";
  script += "set chosenFile to choose file name with prompt \"Save patch"
            "\" default location defaultPath";
  if (!default_name.empty()) {
    script += " default name \"" + escape_for_applescript(default_name) + "\"";
  }
  script += "\nPOSIX path of chosenFile\n";

  std::string output = run_applescript(script);
  if (output.empty()) {
    return DialogResult::Cancelled;
  }
  selected_path = std::filesystem::path(output);
  return DialogResult::Ok;
#else
  if (!initialize()) {
    return DialogResult::Error;
  }

  FilterItems filter_items = build_filter_items(filters);
  const std::string default_utf8 =
      default_dir.empty() ? std::string() : default_dir.string();

  const nfdu8filteritem_t *filter_ptr = nullptr;
  nfdfiltersize_t filter_count = 0;
  if (!filter_items.items.empty()) {
    filter_ptr = filter_items.items.data();
    filter_count = static_cast<nfdfiltersize_t>(filter_items.items.size());
  }

  nfdu8char_t *out_path = nullptr;
  nfdresult_t result =
      NFD_SaveDialogU8(&out_path, filter_ptr, filter_count,
                       default_utf8.empty() ? nullptr : default_utf8.c_str(),
                       default_name.empty() ? nullptr : default_name.c_str());
  if (result == NFD_OKAY) {
    selected_path = std::filesystem::path(out_path);
    NFD_FreePathU8(out_path);
    return DialogResult::Ok;
  }
  if (result == NFD_CANCEL) {
    return DialogResult::Cancelled;
  }
  const char *error = NFD_GetError();
  if (error != nullptr) {
    std::cerr << "Error selecting file: " << error << std::endl;
  }
  return DialogResult::Error;
#endif
}

} // namespace platform::file_dialog
