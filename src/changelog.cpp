#include "changelog.hpp"

#include <span>

namespace megatoy {
namespace {

constexpr std::string_view kEnvelopeDetails[] = {
    "SSG-EG loops are drawn as they sound",
    "Played notes show where they are on the envelope",
};

constexpr std::string_view kFormatDetails[] = {
    ".spat, .tyi, .y12, .dat and .ins",
    ".spat, .tyi and .y12 can also be saved",
};

constexpr std::string_view kEnvelopeAccuracyDetails[] = {
    "The dots can be dragged with SSG-EG on",
    "Warns when a fast SSG-EG decay can skip the sustain level",
};
constexpr std::string_view kPlaybackDetails[] = {
    "macOS: about 9 ms to the device, from about 35",
    "Web: no more crackling while the page is busy",
};
constexpr ChangelogItem kItems_0_10_0[] = {
    {"Easier to use on phones and in narrow windows", {}},
    {"Open several patches at once, each in its own tab", {}},
    {"Give patches cute instrument icons", {}},
    {"Quick Edit in the patch editor shapes a whole patch with five sliders",
     {}},
    {"Envelope graphs are more accurate", kEnvelopeAccuracyDetails},
    {"Smoother playback on the web and macOS", kPlaybackDetails},
    {"20 more scales on the Soft Keyboard", {}},
    {"Added 41 built-in FM patches", {}},
    {"The LFO and operator switches are in the menu bar, and no longer saved "
     "in patches",
     {}},
};

constexpr ChangelogItem kItems_0_9_3[] = {
    {"Drag the dots on the envelope graph to shape the envelope", {}},
    {"New folders and duplicates from the patch browser's context menu", {}},
    {"Notes play at standard pitch, with key scaling as on hardware", {}},
    {"Added 5 patch formats", kFormatDetails},
};

constexpr ChangelogItem kItems_0_9_2[] = {
    {"A taller Total Level slider, beside the envelope graph", {}},
    {"Download on the web offers every format Save As does", {}},
};

constexpr ChangelogItem kItems_0_9_1[] = {
    {"Nuked-OPN2 emulation core, in Preferences", {}},
    {"Audio load meter in the menu bar", {}},
};

constexpr ChangelogItem kItems_0_9_0[] = {
    {"Envelope graphs match the chip, on a millisecond axis", kEnvelopeDetails},
    {"Added 10 built-in FM patches", {}},
};

constexpr ChangelogItem kItems_0_8_4[] = {
    {"Fixed layout at larger interface scales", {}},
};

constexpr ChangelogItem kItems_0_8_3[] = {
    {"Interface scale option in Preferences", {}},
    {"Added 7 built-in FM patches", {}},
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
    {"v0.10.0", kItems_0_10_0}, {"v0.9.3", kItems_0_9_3},
    {"v0.9.2", kItems_0_9_2},   {"v0.9.1", kItems_0_9_1},
    {"v0.9.0", kItems_0_9_0},   {"v0.8.4", kItems_0_8_4},
    {"v0.8.3", kItems_0_8_3},   {"v0.8.2", kItems_0_8_2},
    {"v0.8.1", kItems_0_8_1},   {"v0.8.0", kItems_0_8_0},
    {"v0.6.0", kItems_0_6_0},
};

} // namespace

std::span<const ChangelogEntry> changelog() { return kEntries; }

} // namespace megatoy
