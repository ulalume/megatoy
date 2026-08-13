#pragma once

#include <optional>
#include <string>

namespace platform::web {

std::optional<std::string> read_local_storage(const std::string &key);
bool write_local_storage(const std::string &key, const std::string &value);
bool remove_local_storage(const std::string &key);
/// Remove only if the key still contains `expected_value`.
bool remove_local_storage_if_equals(const std::string &key,
                                    const std::string &expected_value);

} // namespace platform::web
