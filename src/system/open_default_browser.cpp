#include "open_default_browser.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>

#ifdef _WIN32
#include <windows.h>

#include <shellapi.h>
#elif defined(__linux__)
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace megatoy {
namespace system {
namespace {

bool is_valid_url(std::string_view url) {
  if (url.empty()) {
    return false;
  }

  const std::string url_str(url);
  const std::string lower_url = [&]() {
    std::string result;
    result.reserve(url_str.size());
    std::transform(url_str.begin(), url_str.end(), std::back_inserter(result),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
  }();

  // Only http and https: file:// would give access to local files.
  if (lower_url.substr(0, 7) == "http://" ||
      lower_url.substr(0, 8) == "https://") {
    return true;
  }

  return false;
}

#ifdef _WIN32
OpenBrowserResult open_browser_windows(std::string_view url) {
  OpenBrowserResult result;

  HINSTANCE shell_result = ShellExecuteA(nullptr,      // hwnd
                                         "open",       // verb
                                         url.data(),   // file
                                         nullptr,      // parameters
                                         nullptr,      // directory
                                         SW_SHOWNORMAL // show command
  );

  // ShellExecute returns a value > 32 on success
  if (reinterpret_cast<intptr_t>(shell_result) > 32) {
    result.success = true;
  } else {
    result.success = false;
    DWORD error = GetLastError();
    result.error_message =
        "Failed to open URL. Windows error code: " + std::to_string(error);
  }

  return result;
}

#elif defined(__linux__)
OpenBrowserResult open_browser_linux(std::string_view url) {
  OpenBrowserResult result;

  // Use fork/exec for safer execution than system()
  // First try execvp to use PATH, then fallback to specific paths
  pid_t pid = fork();
  if (pid == 0) {
    // Child process - convert url to null-terminated string
    std::string url_str(url);
    char *args[] = {const_cast<char *>("xdg-open"),
                    const_cast<char *>(url_str.c_str()), nullptr};

    execvp("xdg-open", args);

    execl("/usr/bin/xdg-open", "xdg-open", url_str.c_str(), nullptr);
    execl("/bin/xdg-open", "xdg-open", url_str.c_str(), nullptr);
    execl("/usr/local/bin/xdg-open", "xdg-open", url_str.c_str(), nullptr);

    _exit(127);
  } else if (pid > 0) {
    int status;
    if (waitpid(pid, &status, 0) == pid) {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        result.success = true;
      } else {
        result.success = false;
        result.error_message = "xdg-open failed with exit code: " +
                               std::to_string(WEXITSTATUS(status));
      }
    } else {
      result.success = false;
      result.error_message = "Failed to wait for child process";
    }
  } else {
    result.success = false;
    result.error_message = "Failed to fork process";
  }

  return result;
}
#endif

} // namespace

OpenBrowserResult open_default_browser(std::string_view url) {
  OpenBrowserResult result;

  if (!is_valid_url(url)) {
    result.success = false;
    result.error_message =
        "Invalid or unsupported URL scheme. Only http:// and "
        "https:// are allowed.";
    return result;
  }

  // Check for dangerous characters that could be used for injection
  const std::string url_str(url);
  if (url_str.find('"') != std::string::npos ||
      url_str.find('\'') != std::string::npos ||
      url_str.find(';') != std::string::npos ||
      url_str.find('&') != std::string::npos ||
      url_str.find('|') != std::string::npos) {
    result.success = false;
    result.error_message = "URL contains potentially dangerous characters";
    return result;
  }

#ifdef _WIN32
  return open_browser_windows(url);
#elif defined(__linux__)
  return open_browser_linux(url);
#else
  result.success = false;
  result.error_message = "Unsupported platform";
  return result;
#endif
}

} // namespace system
} // namespace megatoy
