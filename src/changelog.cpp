#include "changelog.hpp"

#include <span>

namespace megatoy {
namespace {

constexpr std::string_view kEnvelopeDetails[] = {
    "SSG-EG loops are drawn as they sound",
    "Played notes show where they are on the envelope",
};

constexpr ChangelogItem kItems_0_9_0[] = {
    {"Envelope graphs match the chip, on a millisecond axis", kEnvelopeDetails},
    {"Added 10 built-in FM Patches", {}},
};

constexpr ChangelogItem kItems_0_8_4[] = {
    {"Fixed layout at larger interface scales", {}},
};

constexpr ChangelogItem kItems_0_8_3[] = {
    {"Interface scale option in Preferences", {}},
    {"Added 7 built-in FM Patches", {}},
};

constexpr std::string_view kOperatorDetails[] = {
    "Click an operator to select it, shift-click to add more",
    "Copy, paste and swap operators",
};

constexpr ChangelogItem kItems_0_8_2[] = {
    {"Select operators and edit them together", kOperatorDetails},
    {"Preferences are grouped into tabs", {}},
    {"Audio buffer size can be set in Preferences", {}},
    {"MML Console copies to the clipboard again on the web", {}},
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
    {"v0.9.0", kItems_0_9_0}, {"v0.8.4", kItems_0_8_4},
    {"v0.8.3", kItems_0_8_3}, {"v0.8.2", kItems_0_8_2},
    {"v0.8.1", kItems_0_8_1}, {"v0.8.0", kItems_0_8_0},
    {"v0.6.0", kItems_0_6_0},
};

} // namespace

std::span<const ChangelogEntry> changelog() { return kEntries; }

} // namespace megatoy
