#pragma once

#include <cstdlib>
#include <string>

namespace ui {

inline const char *reveal_in_file_manager_label() {
#if defined(_WIN32)
  return "Show in Explorer";
#elif defined(__APPLE__)
  return "Reveal in Finder";
#elif defined(__linux__)
  return "Show in File Manager";
#else
  return "Show in File Manager";
#endif
}

inline void reveal_in_file_manager(const std::string &path) {
#if defined(_WIN32)
  std::string command = "explorer /select,\"" + path + "\"";
#elif defined(__APPLE__)
  std::string command = "open -R \"" + path + "\"";
#elif defined(__linux__)
  std::string command = "xdg-open \"" + path + "\"";
#else
  std::string command;
#endif
  if (!command.empty()) {
    std::system(command.c_str());
  }
}

} // namespace ui
