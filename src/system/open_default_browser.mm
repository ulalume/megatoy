#include "open_default_browser.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>

#include <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>
#include <cstdlib>

namespace megatoy {
namespace system {
namespace {

// Validate that the URL is safe to open
bool is_valid_url(std::string_view url) {
  if (url.empty()) {
    return false;
  }

  // Check for basic URL schemes we consider safe
  const std::string url_str(url);
  const std::string lower_url = [&]() {
    std::string result;
    result.reserve(url_str.size());
    std::transform(url_str.begin(), url_str.end(), std::back_inserter(result),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
  }();

  // Only allow http and https schemes for security
  // Note: file:// excluded for security reasons - local file access
  if (lower_url.substr(0, 7) == "http://" ||
      lower_url.substr(0, 8) == "https://") {
    return true;
  }

  return false;
}

OpenBrowserResult open_browser_macos(std::string_view url) {
  OpenBrowserResult result;

  @autoreleasepool {
    // Try modern NSWorkspace API first
    NSString *urlString = [NSString stringWithUTF8String:url.data()];
    if (urlString) {
      NSURL *nsurl = [NSURL URLWithString:urlString];
      if (nsurl) {
        BOOL success = [[NSWorkspace sharedWorkspace] openURL:nsurl];
        if (success) {
          result.success = true;
          return result;
        }
      }
    }

    // Fallback to Launch Services API
    CFStringRef url_string = CFStringCreateWithCString(
        kCFAllocatorDefault, url.data(), kCFStringEncodingUTF8);

    if (url_string) {
      CFURLRef url_ref =
          CFURLCreateWithString(kCFAllocatorDefault, url_string, nullptr);
      if (url_ref) {
        OSStatus status = LSOpenCFURLRef(url_ref, nullptr);
        if (status == noErr) {
          result.success = true;
          CFRelease(url_ref);
          CFRelease(url_string);
          return result;
        }
        CFRelease(url_ref);
      }
      CFRelease(url_string);
    }
  }

  // Final fallback to system command if both APIs fail
  if (!result.success) {
    // Properly escape URL for shell command
    std::string escaped_url;
    escaped_url.reserve(url.size() + 10); // Reserve some extra space
    for (char c : url) {
      if (c == '"' || c == '\\' || c == '$' || c == '`') {
        escaped_url += '\\';
      }
      escaped_url += c;
    }

    std::string command = "open \"" + escaped_url + "\"";
    int exit_code = std::system(command.c_str());
    if (exit_code == 0) {
      result.success = true;
      result.error_message.clear();
    } else {
      result.success = false;
      result.error_message =
          "All native APIs failed and fallback command returned: " +
          std::to_string(exit_code);
    }
  }

  return result;
}

} // namespace

OpenBrowserResult open_default_browser(std::string_view url) {
  OpenBrowserResult result;

  // Validate URL first
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

  return open_browser_macos(url);
}

} // namespace system
} // namespace megatoy
