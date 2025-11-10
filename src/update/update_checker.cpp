#include "update_checker.hpp"
#include "platform/platform_config.hpp"
#include "project_info.hpp"
#include <sstream>

namespace update {

std::string build_release_page_url() {
  std::ostringstream oss;
  oss << "https://github.com/" << megatoy::kGithubUser << "/"
      << megatoy::kGithubRepo << "/releases";
  return oss.str();
}

} // namespace update

#if defined(MEGATOY_PLATFORM_WEB)

namespace update {

UpdateCheckResult check_for_updates(std::string_view current_version_tag) {
  UpdateCheckResult result{};
  result.success = false;
  result.latest_version = std::string(current_version_tag);
  result.error_message = "Update checks are unavailable in the web build.";
  return result;
}

} // namespace update

#else

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace update {
namespace {

// Build GitHub API URL dynamically
std::string build_github_api_url() {
  std::ostringstream oss;
  oss << "https://api.github.com/repos/" << megatoy::kGithubUser << "/"
      << megatoy::kGithubRepo << "/releases/latest";
  return oss.str();
}

constexpr const char *kUserAgent = "megatoy-update-check";

size_t write_to_string(void *contents, size_t size, size_t nmemb,
                       void *user_data) {
  auto *buffer = static_cast<std::string *>(user_data);
  buffer->append(static_cast<char *>(contents), size * nmemb);
  return size * nmemb;
}

std::string normalize_tag(std::string_view tag) {
  if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) {
    tag.remove_prefix(1);
  }
  return std::string(tag);
}

void ensure_curl_initialized() {
  static std::once_flag init_flag;
  std::call_once(init_flag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

} // namespace

UpdateCheckResult check_for_updates(std::string_view current_version_tag) {
  ensure_curl_initialized();

  UpdateCheckResult result;
  std::string github_url = build_github_api_url();

  CURL *curl = curl_easy_init();
  if (!curl) {
    result.error_message = "Failed to initialize libcurl.";
    return result;
  }

  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, github_url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  CURLcode perform_result = curl_easy_perform(curl);
  long response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  curl_easy_cleanup(curl);

  if (perform_result != CURLE_OK) {
    result.error_message = curl_easy_strerror(perform_result);
    return result;
  }

  if (response_code != 200) {
    result.error_message =
        "Unexpected HTTP status: " + std::to_string(response_code);
    return result;
  }

  try {
    auto json = nlohmann::json::parse(response);
    result.latest_version = json.value("tag_name", std::string{});
    result.release_url = json.value("html_url", std::string{});

    if (result.latest_version.empty() || result.release_url.empty()) {
      result.error_message = "Incomplete response from update server.";
      return result;
    }

    auto normalized_latest = normalize_tag(result.latest_version);
    auto normalized_current = normalize_tag(current_version_tag);
    result.update_available = normalized_latest != normalized_current;
    result.success = true;
  } catch (const std::exception &ex) {
    result.error_message = ex.what();
  }

  return result;
}

} // namespace update

#endif
