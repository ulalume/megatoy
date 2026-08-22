#include "changelog.hpp"

#include <array>

namespace megatoy {
namespace {

constexpr std::array<std::string_view, 3> kLibraryDetails = {
    "Folders are scanned in the background, so adding a big one no longer "
    "freezes the app",
    "Parsed patches are cached between launches",
    "The browser draws only the rows on screen",
};

constexpr std::array<ChangelogItem, 4> kItems_0_8_1 = {{
    {"YM2612 / YM3438 chip type option", {}},
    {"Large patch libraries load and browse far faster", kLibraryDetails},
    {"The patch you had open is restored on the next launch", {}},
    {"Deleting a folder in the browser now survives a reload", {}},
}};

constexpr std::array<ChangelogItem, 3> kItems_0_8_0 = {{
    {"MIDI pitch bend and mod wheel", {}},
    {"Fixed a crash while rendering long notes", {}},
    {"Fixed patch folders in the browser losing their contents", {}},
}};

constexpr std::array<ChangelogItem, 1> kItems_0_6_0 = {{
    {"Share a patch as a link to the web version", {}},
}};

constexpr std::array<ChangelogEntry, 3> kEntries = {{
    {"v0.8.1", kItems_0_8_1},
    {"v0.8.0", kItems_0_8_0},
    {"v0.6.0", kItems_0_6_0},
}};

} // namespace

std::span<const ChangelogEntry> changelog() { return kEntries; }

} // namespace megatoy
