#include "platform/web/local_storage.hpp"
#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)
#include <emscripten/val.h>

namespace {

emscripten::val storage() {
  return emscripten::val::global("localStorage");
}

} // namespace

namespace platform::web {

std::optional<std::string>
read_local_storage(const std::string &key) {
  using emscripten::val;
  val value = storage().call<val>("getItem", key);
  if (value.isNull() || value.isUndefined()) {
    return std::nullopt;
  }
  return value.as<std::string>();
}

void write_local_storage(const std::string &key,
                         const std::string &value) {
  storage().call<void>("setItem", key, value);
}

void remove_local_storage(const std::string &key) {
  storage().call<void>("removeItem", key);
}

} // namespace platform::web

#else

namespace platform::web {

std::optional<std::string>
read_local_storage(const std::string &) {
  return std::nullopt;
}

void write_local_storage(const std::string &,
                         const std::string &) {}

void remove_local_storage(const std::string &) {}

} // namespace platform::web

#endif

