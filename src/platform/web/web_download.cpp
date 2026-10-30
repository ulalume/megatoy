#include "platform/web/web_download.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace {

#if defined(__EMSCRIPTEN__)
// clang-format off
EM_JS(void, megatoy_download_bytes,
      (const char *filename, const uint8_t *data, int length, const char *mime),
      {
        const name = UTF8ToString(filename);
        const type = UTF8ToString(mime);
        const bytes = HEAPU8.slice(data, data + length);
        const blob = new Blob([bytes], { type });
        const link = document.createElement('a');
        link.href = URL.createObjectURL(blob);
        link.download = name;
        link.style.display = 'none';
        document.body.appendChild(link);
        link.click();
        setTimeout(
            function() {
              URL.revokeObjectURL(link.href);
              document.body.removeChild(link);
            },
            0);
      });

// clang-format on

#endif

} // namespace

namespace platform::web {

void download_binary(const std::string &filename,
                     const std::vector<uint8_t> &data,
                     std::string_view mime_type) {
#if defined(__EMSCRIPTEN__)
  megatoy_download_bytes(filename.c_str(), data.data(),
                         static_cast<int>(data.size()),
                         std::string(mime_type).c_str());
#else
  (void)filename;
  (void)data;
  (void)mime_type;
#endif
}

} // namespace platform::web
