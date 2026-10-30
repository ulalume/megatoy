#pragma once

#include "patches/patch_document.hpp"

struct AppContext;

namespace ui::renderer_detail {

void close_document_now(AppContext &ctx, patches::DocumentId id);
void request_close_document(AppContext &ctx, patches::DocumentId id);
void save_and_close_document(AppContext &ctx, patches::DocumentId id);
void save_and_exit(AppContext &ctx, patches::DocumentId id);
void follow_outside_changes(AppContext &ctx, bool check_outside_files);
void handle_close_shortcut(AppContext &ctx);
void handle_new_shortcut(AppContext &ctx);
void prune_closed_documents(AppContext &ctx);

} // namespace ui::renderer_detail
