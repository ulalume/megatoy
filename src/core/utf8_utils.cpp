#include "core/utf8_utils.hpp"

#include <algorithm>

namespace megatoy::utf8 {
namespace {

bool is_continuation(unsigned char byte) { return (byte & 0xc0u) == 0x80u; }

std::size_t sequence_length(unsigned char lead) {
  if (lead <= 0x7fu) {
    return 1;
  }
  if (lead >= 0xc2u && lead <= 0xdfu) {
    return 2;
  }
  if (lead >= 0xe0u && lead <= 0xefu) {
    return 3;
  }
  if (lead >= 0xf0u && lead <= 0xf4u) {
    return 4;
  }
  return 0;
}

} // namespace

std::string trim_incomplete_suffix(std::string_view input) {
  if (input.empty()) {
    return {};
  }

  std::size_t continuation_start = input.size();
  while (continuation_start > 0 && is_continuation(static_cast<unsigned char>(
                                       input[continuation_start - 1]))) {
    --continuation_start;
  }
  if (continuation_start == 0) {
    return {};
  }

  const std::size_t lead_index = continuation_start - 1;
  const std::size_t expected =
      sequence_length(static_cast<unsigned char>(input[lead_index]));
  const std::size_t available = input.size() - lead_index;
  if (expected == 0) {
    return std::string(input.substr(0, lead_index));
  }
  if (expected == 1) {
    return std::string(input.substr(0, continuation_start));
  }
  if (available < expected) {
    return std::string(input.substr(0, lead_index));
  }
  return std::string(input.substr(0, lead_index + expected));
}

std::string truncate(std::string_view input, std::size_t max_bytes) {
  return trim_incomplete_suffix(
      input.substr(0, std::min(input.size(), max_bytes)));
}

} // namespace megatoy::utf8
