#include "platform/trash.hpp"

#include "core/utf8_utils.hpp"

#import <Foundation/Foundation.h>

namespace platform::trash {

bool available() { return true; }

std::optional<std::filesystem::path>
move_to_trash(const std::filesystem::path &path) {
  @autoreleasepool {
    NSURL *url =
        [NSURL fileURLWithPath:@(megatoy::utf8::from_path(path).c_str())];
    NSURL *trashed = nil;
    if (![NSFileManager.defaultManager trashItemAtURL:url
                                     resultingItemURL:&trashed
                                                error:nil] ||
        trashed == nil) {
      return std::nullopt;
    }
    return megatoy::utf8::to_path(trashed.fileSystemRepresentation);
  }
}

} // namespace platform::trash
