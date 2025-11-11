#include "mml_console.hpp"
#include "formats/ctrmml.hpp"
#include "platform/platform_config.hpp"
#include <cstring>
#include <imgui.h>
#if defined(MEGATOY_PLATFORM_WEB)
#include <emscripten/em_js.h>

EM_JS(void, megatoy_try_copy_text, (const char *data), {
  var text = UTF8ToString(data);

  function createOverlay() {
    var overlay = document.createElement('dialog');
    overlay.id = 'megatoy-clipboard-overlay';
    overlay.style.zIndex = '9999';

    var heading = document.createElement('h2');
    heading.textContent = 'Copy to clipboard manually';
    overlay.appendChild(heading);

    var instructions = document.createElement('div');
    instructions.textContent =
        'Clipboard access is blocked. Focus the text below and press Ctrl+C (Cmd+C on macOS).';
    instructions.style.marginBottom = '10px';
    overlay.appendChild(instructions);

    var textarea = document.createElement('textarea');
    textarea.id = 'megatoy-clipboard-overlay-text';
    textarea.rows = 8;
    textarea.style.width = '100%';
    textarea.style.marginBottom = '10px';
    overlay.appendChild(textarea);

    var closeBtn = document.createElement('button');
    closeBtn.textContent = 'Close';
    closeBtn.style.padding = '4px 12px';
    closeBtn.addEventListener('click', function() { removeOverlay(); });
    overlay.appendChild(closeBtn);
    overlay._megatoyBlocker = function(event) {
      if (!overlay.contains(event.target)) {
        event.stopPropagation();
      }
    };
    document.addEventListener('keydown', overlay._megatoyBlocker, true);
    overlay.addEventListener(
        'keydown', function(event) {
          event.stopPropagation();
          if (event.key == 'Escape') {
            removeOverlay();
          }
        });
    document.body.appendChild(overlay);
    return overlay;
  }

  function removeOverlay() {
    var existing = document.getElementById('megatoy-clipboard-overlay');
    if (existing) {
      if (existing._megatoyBlocker) {
        document.removeEventListener('keydown', existing._megatoyBlocker, true);
      }
      if (existing.parentNode) {
        existing.parentNode.removeChild(existing);
      }
    }
  }

  function showOverlay() {
    var overlay = document.getElementById('megatoy-clipboard-overlay');
    if (!overlay) {
      overlay = createOverlay();
    }
    var textarea = document.getElementById('megatoy-clipboard-overlay-text');
    if (textarea) {
      textarea.value = text;
      textarea.focus();
      textarea.select();
    }
    overlay.showModal();
  }

  if (navigator && navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(text).catch(function() { showOverlay(); });
  } else {
    showOverlay();
  }
});
#endif

namespace ui {

void render_mml_console(const char *title, MmlConsoleContext &context) {
  ImGui::SetNextWindowSize(ImVec2(300, 180), ImGuiCond_FirstUseEver);

  if (!context.ui_prefs.show_mml_console) {
    return;
  }
  if (!ImGui::Begin(title, &context.ui_prefs.show_mml_console)) {
    ImGui::End();
    return;
  }

  auto mml = formats::ctrmml::patch_to_string(context.current_patch());
  auto mml_c = mml.c_str();

  // copy to clipboard button
  if (ImGui::Button("Copy to Clipboard")) {
#if defined(MEGATOY_PLATFORM_WEB)
    megatoy_try_copy_text(mml_c);
#else
    ImGui::SetClipboardText(mml_c);
#endif
  }
  // panel (border)
  ImGui::BeginChild("MML Panel", ImVec2(0, 0), true);
  ImGui::Text("%s", mml_c);
  ImGui::EndChild();

  ImGui::End();
};

}; // namespace ui
