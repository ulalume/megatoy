#pragma once

#include <filesystem>
#include <vector>

namespace ui::os_file_drop {

/// The payload ImGui carries while files are dragged in from outside.
inline constexpr const char *kPayload = "MEGATOY_OS_FILES";

/// The window's drop events, as SDL reports them.
void begin();
void moved();
void add(const std::filesystem::path &path);
void complete();

/**
 * While files are dragged over the window, carries them as ImGui's drag,
 * for the places that take them to answer. A drag that goes quiet ends on
 * its own: one the page took over, or one the platform never finished.
 * Called once a frame.
 */
void submit_source();

/// The files let go of, for the place they were let go over, on the frame
/// they are; or, at its end, for wherever a drop always went. Taking them
/// leaves none.
std::vector<std::filesystem::path> take();

} // namespace ui::os_file_drop
