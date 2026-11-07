#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>

// imgui_impl_sdl3.cpp defines it without declaring it in its header.
ImGuiKey ImGui_ImplSDL3_KeyEventToImGuiKey(SDL_Keycode keycode,
                                           SDL_Scancode scancode);

namespace ui {

/**
 * The key at a place on the keyboard, named as the layout in use has ImGui
 * name it. A place is given by its key on a US keyboard; on a French one, the
 * key at SDL_SCANCODE_A is ImGuiKey_Q.
 */
inline ImGuiKey key_at(SDL_Scancode place) {
  return ImGui_ImplSDL3_KeyEventToImGuiKey(
      SDL_GetKeyFromScancode(place, SDL_KMOD_NONE, false), place);
}

} // namespace ui
