#pragma once

#include "patches/patch_repository.hpp"
#include "platform/platform_config.hpp"

struct AppContext;

namespace ui::renderer_detail {

void request_workspace_folder_removal(AppContext &ctx,
                                      const std::filesystem::path &path);
void request_patch_deletion(AppContext &ctx, const patches::PatchEntry &entry);
void request_patch_move(AppContext &ctx, const patches::PatchEntry &entry,
                        const patches::PatchEntry &destination);
void request_patch_rename(AppContext &ctx, const patches::PatchEntry &entry);
void request_new_patch(AppContext &ctx, const std::filesystem::path &folder);
void request_new_folder(AppContext &ctx, const std::filesystem::path &parent);
void request_patch_duplicate(AppContext &ctx, const patches::PatchEntry &entry);
void render_patch_duplicate_dialog(AppContext &ctx);
#if defined(MEGATOY_PLATFORM_WEB)
void download_patch_entry(AppContext &ctx, const patches::PatchEntry &entry,
                          const std::string &extension);
#endif

} // namespace ui::renderer_detail
