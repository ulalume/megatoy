#include "changelog.hpp"

#include <span>

namespace megatoy {
namespace {

constexpr std::string_view kOperatorDetails[] = {
    "Click an operator to select it, shift-click to add more",
    "Copy, paste and swap operators",
};

constexpr ChangelogItem kItems_0_8_2[] = {
    {"Select operators and edit them together", kOperatorDetails},
    {"Preferences are grouped into tabs", {}},
    {"Audio buffer size can be set in Preferences", {}},
};

constexpr std::string_view kLibraryDetails[] = {
    "Folders are scanned in the background, so adding a big one no longer "
    "freezes the app",
};

constexpr ChangelogItem kItems_0_8_1[] = {
    {"YM2612 / YM3438 chip type option", {}},
    {"Large patch libraries load and browse far faster", kLibraryDetails},
};

constexpr ChangelogItem kItems_0_8_0[] = {
    {"MIDI pitch bend and mod wheel", {}},
};

constexpr ChangelogItem kItems_0_6_0[] = {
    {"Share a patch as a link to the web version", {}},
};

constexpr ChangelogEntry kEntries[] = {
    {"v0.8.2", kItems_0_8_2},
    {"v0.8.1", kItems_0_8_1},
    {"v0.8.0", kItems_0_8_0},
    {"v0.6.0", kItems_0_6_0},
};

} // namespace

std::span<const ChangelogEntry> changelog() { return kEntries; }

} // namespace megatoy
