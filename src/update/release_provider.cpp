#include "update/release_provider.hpp"

#include <future>
#include <mutex>
#include <string>

namespace update {
namespace {

std::shared_ptr<ReleaseInfoProvider> &provider_storage() {
  static std::shared_ptr<ReleaseInfoProvider> provider =
      std::make_shared<CurlReleaseInfoProvider>();
  return provider;
}

} // namespace

std::future<UpdateCheckResult> CurlReleaseInfoProvider::check_for_updates_async(
    std::string_view current_version_tag) {
  const std::string version(current_version_tag);
  return std::async(std::launch::async,
                    [version]() { return check_for_updates(version); });
}

void set_release_info_provider(std::shared_ptr<ReleaseInfoProvider> provider) {
  if (!provider) {
    return;
  }
  provider_storage() = std::move(provider);
}

ReleaseInfoProvider &release_info_provider() { return *provider_storage(); }

} // namespace update
