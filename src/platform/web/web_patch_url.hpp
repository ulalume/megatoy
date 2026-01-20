#pragma once

#include "ym2612/patch.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace platform::web::patch_url {

constexpr int kQueryVersion = 1;

std::string build_query(const ym2612::Patch &patch);

std::optional<ym2612::Patch>
parse_query(std::string_view query, const ym2612::Patch &defaults,
            std::string *error = nullptr);

std::optional<ym2612::Patch>
load_patch_from_current_url(const ym2612::Patch &defaults,
                            std::string *error = nullptr);

void sync_patch_to_url_if_needed(const ym2612::Patch &patch);

} // namespace platform::web::patch_url
