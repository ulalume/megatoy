#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace patches {
class PatchSession;
}

namespace ui::library_undo {

/**
 * Says `message` on a toast with an Undo button that runs `undo`, in place of
 * any such offer before it. The offer lasts as long as the toast, and is
 * withdrawn once anything at `touched` changes on disk or is edited in an
 * open patch.
 */
void offer(std::string message, std::vector<std::filesystem::path> touched,
           std::function<void()> undo);

/// Withdraws the offer when what it would undo has changed since. Called
/// once a frame.
void check(const patches::PatchSession &session);

} // namespace ui::library_undo
