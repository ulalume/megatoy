#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace megatoy::utf8 {

std::string trim_incomplete_suffix(std::string_view input);
std::string truncate(std::string_view input, std::size_t max_bytes);

} // namespace megatoy::utf8
