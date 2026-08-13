#include "platform/web/local_storage.hpp"
#include "platform/platform_config.hpp"

#if defined(MEGATOY_PLATFORM_WEB)
#include <cstdlib>
#include <emscripten.h>

namespace {

// clang-format off
EM_JS(char *, read_local_storage_value, (const char *key), {
  try {
    if (typeof localStorage == "undefined")
      return 0;
    const value = localStorage.getItem(UTF8ToString(key));
    if (value == null)
      return 0;
    const size = lengthBytesUTF8(value) + 1;
    const result = _malloc(size);
    if (!result)
      return 0;
    stringToUTF8(value, result, size);
    return result;
  } catch (e) {
    console.error("localStorage.getItem failed", e);
    return 0;
  }
});
// clang-format on

} // namespace

namespace platform::web {

std::optional<std::string> read_local_storage(const std::string &key) {
  char *value = read_local_storage_value(key.c_str());
  if (value == nullptr) {
    return std::nullopt;
  }
  std::string result(value);
  std::free(value);
  return result;
}

bool write_local_storage(const std::string &key, const std::string &value) {
  // Wrap in try/catch to avoid uncaught exceptions when localStorage is
  // unavailable (e.g., private mode or blocked).
  // clang-format off
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
  // clang-format on
}

bool remove_local_storage(const std::string &key) {
  // clang-format off
  return EM_ASM_INT(
             {
               try {
                 const k = UTF8ToString($0);
                 if (typeof localStorage === "undefined") return 0;
                 localStorage.removeItem(k);
                 return 1;
               } catch (e) {
                 console.error("localStorage.removeItem failed", e);
                 return 0;
               }
             },
      key.c_str()) != 0;
  // clang-format on
}

bool remove_local_storage_if_equals(const std::string &key,
                                    const std::string &expected_value) {
  // Keep compare + remove in one JavaScript call so another old-version tab
  // cannot replace the library between two separate operations.
  // clang-format off
  return EM_ASM_INT(
             {
               try {
                 const k = UTF8ToString($0);
                 const expected = UTF8ToString($1);
                 if (typeof localStorage === "undefined") return 0;
                 if (localStorage.getItem(k) !== expected) return 0;
                 localStorage.removeItem(k);
                 return 1;
               } catch (e) {
                 console.error("conditional localStorage.removeItem failed", e);
                 return 0;
               }
             },
      key.c_str(), expected_value.c_str()) != 0;
  // clang-format on
}

} // namespace platform::web

#else

namespace platform::web {

std::optional<std::string> read_local_storage(const std::string &) {
  return std::nullopt;
}

bool write_local_storage(const std::string &, const std::string &) {
  return false;
}

bool remove_local_storage(const std::string &) { return false; }

bool remove_local_storage_if_equals(const std::string &, const std::string &) {
  return false;
}

} // namespace platform::web

#endif
