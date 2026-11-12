#include "platform/file_dialog.hpp"

#include "core/utf8_utils.hpp"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

/**
 * The Format popup under the save panel. Each item allows its format's
 * types alone, and the panel changes the name's extension to match.
 */
@interface MegatoyFormatPicker : NSObject
- (instancetype)initWithPanel:(NSSavePanel *)panel
                        types:(NSArray<NSArray<UTType *> *> *)types;
- (void)formatChosen:(NSPopUpButton *)popup;
@end

@implementation MegatoyFormatPicker {
  __weak NSSavePanel *_panel;
  NSArray<NSArray<UTType *> *> *_types;
}

- (instancetype)initWithPanel:(NSSavePanel *)panel
                        types:(NSArray<NSArray<UTType *> *> *)types {
  if ((self = [super init])) {
    _panel = panel;
    _types = types;
  }
  return self;
}

- (void)formatChosen:(NSPopUpButton *)popup {
  _panel.allowedContentTypes = _types[popup.indexOfSelectedItem];
}

@end

namespace platform::file_dialog {

namespace {

/// Empty rather than nil for bytes that are not UTF-8, which a patch's name
/// from an older format may be.
NSString *to_ns(const std::string &utf8) {
  return [[NSString alloc] initWithBytes:utf8.data()
                                  length:utf8.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

NSString *to_ns(const std::filesystem::path &path) {
  return to_ns(megatoy::utf8::from_path(path));
}

NSArray<UTType *> *types_of(const FileFilter &filter) {
  NSMutableArray<UTType *> *types = [NSMutableArray array];
  for (const auto &extension : filter.extensions) {
    if (UTType *type = [UTType typeWithFilenameExtension:to_ns(extension)
                                        conformingToType:UTTypeData]) {
      [types addObject:type];
    }
  }
  return types;
}

NSView *format_row(NSPopUpButton *popup) {
  NSTextField *label = [NSTextField labelWithString:@"Format:"];
  NSStackView *row = [NSStackView stackViewWithViews:@[ label, popup ]];
  row.edgeInsets = NSEdgeInsetsMake(12, 20, 12, 20);
  [row setFrameSize:row.fittingSize];
  return row;
}

} // namespace

DialogResult save_file(const std::filesystem::path &default_dir,
                       const std::string &stem,
                       const std::vector<FileFilter> &filters,
                       std::filesystem::path &selected_path) {
  @autoreleasepool {
    NSWindow *key_window = NSApp.keyWindow;
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.extensionHidden = NO;

    NSMutableArray<NSArray<UTType *> *> *types = [NSMutableArray array];
    for (const auto &filter : filters) {
      [types addObject:types_of(filter)];
    }
    // Kept for as long as the panel runs: the popup holds its target weakly.
    MegatoyFormatPicker *picker __attribute__((objc_precise_lifetime)) =
        [[MegatoyFormatPicker alloc] initWithPanel:panel types:types];
    if (types.count > 0) {
      panel.allowedContentTypes = types[0];
    }
    if (types.count > 1) {
      NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect
                                                        pullsDown:NO];
      for (const auto &filter : filters) {
        [popup addItemWithTitle:to_ns(filter.label)];
      }
      popup.target = picker;
      popup.action = @selector(formatChosen:);
      panel.accessoryView = format_row(popup);
    }

    if (!default_dir.empty()) {
      panel.directoryURL = [NSURL fileURLWithPath:to_ns(default_dir)
                                      isDirectory:YES];
    }
    panel.nameFieldStringValue =
        to_ns(name_with_first_extension(stem, filters));

    const NSModalResponse response = [panel runModal];
    [key_window makeKeyAndOrderFront:nil];
    if (response != NSModalResponseOK) {
      return DialogResult::Cancelled;
    }
    const char *path = panel.URL.path.UTF8String;
    if (path == nullptr) {
      return DialogResult::Error;
    }
    selected_path = megatoy::utf8::to_path(path);
    return DialogResult::Ok;
  }
}

} // namespace platform::file_dialog
