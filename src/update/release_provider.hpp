#pragma once

#include "update/update_checker.hpp"
#include <future>
#include <memory>
#include <string_view>

namespace update {

class ReleaseInfoProvider {
public:
  virtual ~ReleaseInfoProvider() = default;
  virtual std::future<UpdateCheckResult>
  check_for_updates_async(std::string_view current_version_tag) = 0;
};

class CurlReleaseInfoProvider : public ReleaseInfoProvider {
public:
  std::future<UpdateCheckResult>
  check_for_updates_async(std::string_view current_version_tag) override;
};

void set_release_info_provider(std::shared_ptr<ReleaseInfoProvider> provider);
ReleaseInfoProvider &release_info_provider();

} // namespace update
