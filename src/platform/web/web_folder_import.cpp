#include "platform/web/web_folder_import.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include <emscripten.h>
#include <memory>
#include <string>
#include <utility>

namespace platform::web {

namespace {

// The JS side owns the callback pointer until it fires exactly once.
struct PendingImport {
  std::function<void(FolderImportResult)> on_complete;
};

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void megatoy_folder_import_done(void *handle, int ok,
                                                     const char *folder_name,
                                                     const char *path,
                                                     int file_count,
                                                     const char *error) {
  std::unique_ptr<PendingImport> pending(static_cast<PendingImport *>(handle));
  if (!pending || !pending->on_complete) {
    return;
  }

  FolderImportResult result;
  result.ok = ok != 0;
  result.folder_name = folder_name != nullptr ? folder_name : "";
  result.path = path != nullptr ? std::filesystem::path(path)
                                : std::filesystem::path();
  result.file_count = file_count > 0 ? static_cast<std::size_t>(file_count) : 0;
  result.error = error != nullptr ? error : "";
  pending->on_complete(std::move(result));
}

EMSCRIPTEN_KEEPALIVE void megatoy_folder_import_cancelled(void *handle) {
  delete static_cast<PendingImport *>(handle);
}

} // extern "C"

// clang-format off
EM_JS(void, megatoy_open_folder_import, (const char *destination,
                                         void *handle), {
  var destRoot = UTF8ToString(destination);

  var input = document.createElement("input");
  input.type = "file";
  // webkitdirectory is the only directory selection every browser implements.
  // showDirectoryPicker() would give a nicer dialog but exists solely in
  // Chromium; Firefox and Safari ship no local directory picker at all.
  input.webkitdirectory = true;
  input.multiple = true;
  input.style.display = "none";
  document.body.appendChild(input);

  var settled = false;
  function finish(ok, folderName, path, count, error) {
    if (settled) return;
    settled = true;
    input.remove();
    var namePtr = stringToNewUTF8(folderName || "");
    var pathPtr = stringToNewUTF8(path || "");
    var errPtr = stringToNewUTF8(error || "");
    Module["_megatoy_folder_import_done"](handle, ok ? 1 : 0, namePtr, pathPtr,
                                          count | 0, errPtr);
    _free(namePtr); _free(pathPtr); _free(errPtr);
  }

  function cancel() {
    if (settled) return;
    settled = true;
    input.remove();
    Module["_megatoy_folder_import_cancelled"](handle);
  }

  input.addEventListener("cancel", cancel);

  input.addEventListener("change", async function() {
    var files = Array.from(input.files || []);
    if (files.length === 0) { cancel(); return; }

    // webkitRelativePath is "<chosen folder>/<subdirs>/<file>". Take the
    // first segment as the folder name and keep the rest of the structure.
    var first = files[0].webkitRelativePath || files[0].name;
    var folderName = first.split("/")[0] || "Imported";

    // Never collide with an existing import: append a counter if needed.
    var target = destRoot + "/" + folderName;
    var unique = target;
    var suffix = 2;
    while (FS.analyzePath(unique).exists) {
      unique = target + " (" + suffix + ")";
      suffix++;
    }

    function mkdirp(dir) {
      var parts = dir.split("/").filter(function(p) { return p.length > 0; });
      var built = "";
      for (var i = 0; i < parts.length; i++) {
        built += "/" + parts[i];
        if (!FS.analyzePath(built).exists) {
          try { FS.mkdir(built); } catch (e) { /* raced or exists */ }
        }
      }
    }

    var written = 0;
    try {
      mkdirp(unique);
      for (var i = 0; i < files.length; i++) {
        var file = files[i];
        var rel = file.webkitRelativePath || file.name;
        // Drop the chosen folder's own name; `unique` already stands in for it.
        var slash = rel.indexOf("/");
        var inner = slash >= 0 ? rel.substring(slash + 1) : rel;
        if (!inner) continue;

        var full = unique + "/" + inner;
        var dir = full.substring(0, full.lastIndexOf("/"));
        mkdirp(dir);

        var buffer = await file.arrayBuffer();
        FS.writeFile(full, new Uint8Array(buffer));
        written++;
      }
    } catch (e) {
      finish(false, folderName, unique, written, "" + e);
      return;
    }

    // autoPersist batches writes, but flush now so a reload right after an
    // import does not lose it.
    FS.syncfs(false, function(err) {
      if (err) {
        finish(false, folderName, unique, written, "" + err);
      } else {
        finish(true, folderName, unique, written, "");
      }
    });
  });

  input.click();
});
// clang-format on

void import_folder(const std::filesystem::path &destination_root,
                   std::function<void(FolderImportResult)> on_complete) {
  auto *pending = new PendingImport{std::move(on_complete)};
  megatoy_open_folder_import(destination_root.string().c_str(), pending);
}

} // namespace platform::web

#endif
