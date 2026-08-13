#include "version.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <vector>

namespace update {
namespace {

struct PrereleaseIdentifier {
  bool numeric = false;
  std::uint64_t number = 0;
  std::string text;
};

struct Version {
  std::array<std::uint64_t, 3> core{};
  std::vector<PrereleaseIdentifier> prerelease;
};

bool is_identifier_character(char value) {
  return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z') || value == '-';
}

bool parse_number(std::string_view value, std::uint64_t &number) {
  if (value.empty() || !std::all_of(value.begin(), value.end(), [](char c) {
        return c >= '0' && c <= '9';
      })) {
    return false;
  }
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), number);
  return error == std::errc{} && end == value.data() + value.size();
}

bool valid_identifier_list(std::string_view value) {
  while (!value.empty()) {
    const auto separator = value.find('.');
    const auto identifier = value.substr(0, separator);
    if (identifier.empty() || !std::all_of(identifier.begin(), identifier.end(),
                                           is_identifier_character)) {
      return false;
    }
    if (separator == std::string_view::npos) {
      return true;
    }
    value.remove_prefix(separator + 1);
  }
  return false;
}

std::optional<Version> parse_version(std::string_view tag) {
  if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) {
    tag.remove_prefix(1);
  }
  if (tag.empty()) {
    return std::nullopt;
  }

  const auto build_separator = tag.find('+');
  if (build_separator != std::string_view::npos) {
    const auto build = tag.substr(build_separator + 1);
    if (!valid_identifier_list(build)) {
      return std::nullopt;
    }
    tag = tag.substr(0, build_separator);
  }

  std::string_view prerelease;
  const auto prerelease_separator = tag.find('-');
  if (prerelease_separator != std::string_view::npos) {
    prerelease = tag.substr(prerelease_separator + 1);
    if (!valid_identifier_list(prerelease)) {
      return std::nullopt;
    }
    tag = tag.substr(0, prerelease_separator);
  }

  Version version;
  for (std::size_t index = 0; index < version.core.size(); ++index) {
    const auto separator = tag.find('.');
    const auto component = tag.substr(0, separator);
    if (!parse_number(component, version.core[index])) {
      return std::nullopt;
    }
    if (index + 1 == version.core.size()) {
      if (separator != std::string_view::npos) {
        return std::nullopt;
      }
    } else {
      if (separator == std::string_view::npos) {
        return std::nullopt;
      }
      tag.remove_prefix(separator + 1);
    }
  }

  while (!prerelease.empty()) {
    const auto separator = prerelease.find('.');
    const auto value = prerelease.substr(0, separator);
    PrereleaseIdentifier identifier;
    identifier.numeric = parse_number(value, identifier.number);
    if (!identifier.numeric) {
      identifier.text = std::string(value);
    }
    version.prerelease.push_back(std::move(identifier));
    if (separator == std::string_view::npos) {
      break;
    }
    prerelease.remove_prefix(separator + 1);
  }

  return version;
}

int compare(const PrereleaseIdentifier &lhs, const PrereleaseIdentifier &rhs) {
  if (lhs.numeric != rhs.numeric) {
    return lhs.numeric ? -1 : 1;
  }
  if (lhs.numeric) {
    return lhs.number < rhs.number ? -1 : lhs.number > rhs.number ? 1 : 0;
  }
  return lhs.text < rhs.text ? -1 : lhs.text > rhs.text ? 1 : 0;
}

int compare(const Version &lhs, const Version &rhs) {
  for (std::size_t index = 0; index < lhs.core.size(); ++index) {
    if (lhs.core[index] != rhs.core[index]) {
      return lhs.core[index] < rhs.core[index] ? -1 : 1;
    }
  }

  if (lhs.prerelease.empty() != rhs.prerelease.empty()) {
    return lhs.prerelease.empty() ? 1 : -1;
  }
  const auto shared_size =
      std::min(lhs.prerelease.size(), rhs.prerelease.size());
  for (std::size_t index = 0; index < shared_size; ++index) {
    const int result = compare(lhs.prerelease[index], rhs.prerelease[index]);
    if (result != 0) {
      return result;
    }
  }
  return lhs.prerelease.size() < rhs.prerelease.size()   ? -1
         : lhs.prerelease.size() > rhs.prerelease.size() ? 1
                                                         : 0;
}

} // namespace

std::optional<bool> is_version_newer(std::string_view candidate,
                                     std::string_view current) {
  const auto candidate_version = parse_version(candidate);
  const auto current_version = parse_version(current);
  if (!candidate_version || !current_version) {
    return std::nullopt;
  }
  return compare(*candidate_version, *current_version) > 0;
}

} // namespace update
