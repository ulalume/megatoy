#include "platform/web/web_download.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace {

#if defined(__EMSCRIPTEN__)
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

EM_JS(void, megatoy_download_text,
      (const char *filename, const char *text, const char *mime), {
        const name = UTF8ToString(filename);
        const data = UTF8ToString(text);
        const type = UTF8ToString(mime);
        const blob = new Blob([data], { type });
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

void download_text(const std::string &filename, std::string_view text,
                   std::string_view mime_type) {
#if defined(__EMSCRIPTEN__)
  std::string payload(text);
  megatoy_download_text(filename.c_str(), payload.c_str(),
                        std::string(mime_type).c_str());
#else
  (void)filename;
  (void)text;
  (void)mime_type;
#endif
}

} // namespace platform::web
