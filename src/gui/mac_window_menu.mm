#include "mac_window_menu.hpp"

#include <AppKit/AppKit.h>

namespace megatoy::gui {

void release_close_window_shortcut() {
  @autoreleasepool {
    NSMenu *menu = [NSApp windowsMenu];
    for (NSMenuItem *item in [menu itemArray]) {
      if ([item action] == @selector(performClose:)) {
        [item setKeyEquivalent:@""];
      }
    }
  }
}

} // namespace megatoy::gui
