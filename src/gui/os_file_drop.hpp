#pragma once

#include <filesystem>
#include <vector>

namespace ui::os_file_drop {

/// The payload ImGui carries while files are dragged in from outside.
inline constexpr const char *kPayload = "MEGATOY_OS_FILES";

/// The window's drop events, as SDL reports them.
void begin();
void add(const std::filesystem::path &path);
void complete();
/// A drop that is not coming, such as one the page took over itself.
void cancel();

/// While files are dragged over the window, carries them as ImGui's drag,
/// for the places that take them to answer. Called once a frame.
void submit_source();

/// The dropped files, for the target that takes them, on the frame they are
/// let go over it. Taking them leaves none for the next.
std::vector<std::filesystem::path> take();

/// The dropped files no target took, once the frame they were let go is
/// drawn.
std::vector<std::filesystem::path> take_unclaimed();

} // namespace ui::os_file_drop
