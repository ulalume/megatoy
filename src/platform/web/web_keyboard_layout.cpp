#include "platform/web/web_keyboard_layout.hpp"

#include <emscripten.h>
#include <utility>

namespace {

std::vector<platform::host::LayoutKey> g_arrived;

EM_JS_DEPS(megatoy_keyboard_layout, "$stringToNewUTF8");

// clang-format off
EM_JS(void, megatoy_ask_keyboard_layout, (), {
  var keyboard = navigator.keyboard;
  if (!keyboard || !keyboard.getLayoutMap) return;
  keyboard.getLayoutMap().then(function(map) {
    map.forEach(function(typed, place) {
      if (Array.from(typed).length !== 1) return;
      var name = stringToNewUTF8(place);
      Module["_megatoy_keyboard_layout_key"](name, typed.codePointAt(0));
      _free(name);
    });
  }).catch(function() {});
});
// clang-format on

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void megatoy_keyboard_layout_key(const char *place,
                                                      int typed) {
  g_arrived.push_back({place, static_cast<char32_t>(typed)});
}

} // extern "C"

namespace platform::web {

std::vector<host::LayoutKey> take_keyboard_layout() {
  static bool asked = false;
  if (!asked) {
    asked = true;
    megatoy_ask_keyboard_layout();
  }
  return std::exchange(g_arrived, {});
}

} // namespace platform::web
