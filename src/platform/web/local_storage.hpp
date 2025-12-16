#pragma once

#include <optional>
#include <string>

namespace platform::web {

std::optional<std::string> read_local_storage(const std::string &key);
bool write_local_storage(const std::string &key, const std::string &value);
void remove_local_storage(const std::string &key);

} // namespace platform::web
