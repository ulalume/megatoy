#include "platform/web/local_storage.hpp"
#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)
#include <emscripten.h>
#include <emscripten/val.h>

namespace {

emscripten::val storage() { return emscripten::val::global("localStorage"); }

} // namespace

namespace platform::web {

std::optional<std::string> read_local_storage(const std::string &key) {
  using emscripten::val;
  val value = storage().call<val>("getItem", key);
  if (value.isNull() || value.isUndefined()) {
    return std::nullopt;
  }
  return value.as<std::string>();
}

bool write_local_storage(const std::string &key, const std::string &value) {
  // Wrap in try/catch to avoid uncaught exceptions when localStorage is
  // unavailable (e.g., private mode or blocked).
  return EM_ASM_INT(
             {
               try {
                 const k = UTF8ToString($0);
                 const v = UTF8ToString($1);
                 if (typeof localStorage === "undefined") return 0;
                 localStorage.setItem(k, v);
                 return 1;
               } catch (e) {
                 console.error("localStorage.setItem failed", e);
                 return 0;
               }
             },
      key.c_str(), value.c_str()) != 0;
}

void remove_local_storage(const std::string &key) {
  storage().call<void>("removeItem", key);
}

} // namespace platform::web

#else

namespace platform::web {

std::optional<std::string> read_local_storage(const std::string &) {
  return std::nullopt;
}

void write_local_storage(const std::string &, const std::string &) {}

void remove_local_storage(const std::string &) {}

} // namespace platform::web

#endif
