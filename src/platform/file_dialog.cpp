#include "file_dialog.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

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

std::string build_filter_list(const std::vector<FileFilter> &filters) {
  if (filters.empty()) {
    return {};
  }

  std::ostringstream oss;
  bool first_extension = true;
  for (const auto &filter : filters) {
    for (const auto &ext : filter.extensions) {
      if (!first_extension) {
        oss << ',';
      }
      first_extension = false;
      oss << ext;
    }
  }
  return oss.str();
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
    return DialogResult::Cancel;
  }
  selected_path = std::filesystem::path(output);
  return DialogResult::Ok;
#else
  if (!initialize()) {
    return DialogResult::Error;
  }

  nfdchar_t *out_path = nullptr;
  const std::string default_utf8 =
      default_path.empty() ? std::string() : default_path.string();
  nfdresult_t result = NFD_PickFolder(
      default_utf8.empty() ? nullptr : default_utf8.c_str(), &out_path);
  if (result == NFD_OKAY) {
    selected_path = std::filesystem::path(out_path);
    NFD_FreePath(out_path);
    return DialogResult::Ok;
  }
  if (result == NFD_CANCEL) {
    return DialogResult::Cancel;
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
    return DialogResult::Cancel;
  }
  selected_path = std::filesystem::path(output);
  return DialogResult::Ok;
#else
  if (!initialize()) {
    return DialogResult::Error;
  }

  std::string filter_list = build_filter_list(filters);
  const std::string default_utf8 =
      default_dir.empty() ? std::string() : default_dir.string();

  nfdchar_t *out_path = nullptr;
  nfdresult_t result = NFD_SaveDialog(
      filter_list.empty() ? nullptr : filter_list.c_str(),
      default_utf8.empty() ? nullptr : default_utf8.c_str(), &out_path);
  if (result == NFD_OKAY) {
    selected_path = std::filesystem::path(out_path);
    NFD_FreePath(out_path);
    return DialogResult::Ok;
  }
  if (result == NFD_CANCEL) {
    return DialogResult::Cancel;
  }
  const char *error = NFD_GetError();
  if (error != nullptr) {
    std::cerr << "Error selecting file: " << error << std::endl;
  }
  return DialogResult::Error;
#endif
}

} // namespace platform::file_dialog
