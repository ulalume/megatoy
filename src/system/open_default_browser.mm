#include "open_default_browser.hpp"

#include "system/url_check.hpp"

#include <utility>

#include <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>
#include <cstdlib>

namespace megatoy {
namespace system {
namespace {

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
    escaped_url.reserve(url.size() + 10);
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

  if (auto problem = url_problem(url); !problem.empty()) {
    result.success = false;
    result.error_message = std::move(problem);
    return result;
  }

  return open_browser_macos(url);
}

} // namespace system
} // namespace megatoy
