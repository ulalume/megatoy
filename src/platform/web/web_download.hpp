#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace platform::web {

void download_binary(const std::string &filename,
                     const std::vector<uint8_t> &data,
                     std::string_view mime_type);

void download_text(const std::string &filename, std::string_view text,
                   std::string_view mime_type);

} // namespace platform::web
