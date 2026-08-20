#include "platform/web/web_folder_import.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "gui/components/common.hpp"
#include "platform/import_pipeline.hpp"
#include "platform/web/web_storage_persistence.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <emscripten.h>
#include <filesystem>
#include <imgui.h>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace platform::web {

EM_JS_DEPS(megatoy_folder_import, "$stringToNewUTF8");

namespace {

enum class ImportPhase : int {
  AwaitingConfirmation = 0,
  Copying = 1,
  Validating = 2,
  Syncing = 3,
};

struct PendingImport {
  std::function<void(FolderImportResult)> on_complete;
};

struct ImportRequest {
  void *picker_handle = nullptr;
  bool is_drop = false;
  std::string folder_name;
  std::filesystem::path staging_path;
  std::filesystem::path final_path;
  std::size_t kept_count = 0;
  std::uint64_t kept_bytes = 0;
  std::size_t filtered_count = 0;
  std::int64_t quota_remaining = -1;
  std::atomic<ImportPhase> phase{ImportPhase::AwaitingConfirmation};
  std::atomic<std::size_t> current_files{0};
  std::atomic<std::size_t> total_files{0};
  std::atomic<std::uint64_t> current_bytes{0};
  std::atomic<std::uint64_t> total_bytes{0};
  std::atomic<bool> cancel_requested{false};
  std::vector<std::filesystem::path> staged_files;
  std::vector<std::filesystem::path> validated_files;
  std::vector<
      std::pair<std::filesystem::path,
                std::shared_ptr<const import_pipeline::WarmedContainer>>>
      warmed_containers;
  std::vector<import_pipeline::ValidationFailure> failures;
  std::size_t validation_index = 0;
  std::unique_ptr<import_pipeline::ImportStager> stager;
};

struct ResultReport {
  std::string folder_name;
  std::filesystem::path path;
  std::size_t imported_count = 0;
  std::size_t filtered_count = 0;
  std::vector<import_pipeline::ValidationFailure> failures;
  bool open = true;
};

std::function<void(FolderImportResult)> g_drop_handler;
std::unique_ptr<ImportRequest> g_active_import;
std::deque<ResultReport> g_result_reports;
bool g_close_import_popup = false;

std::string format_megabytes(std::uint64_t bytes) {
  std::ostringstream stream;
  stream.setf(std::ios::fixed);
  stream.precision(bytes >= 10 * 1024 * 1024 ? 1 : 2);
  stream << static_cast<double>(bytes) / (1024.0 * 1024.0);
  return stream.str();
}

FolderImportResult make_result(const ImportRequest &request, bool ok,
                               bool cancelled, std::string error) {
  FolderImportResult result;
  result.ok = ok;
  result.cancelled = cancelled;
  result.folder_name = request.folder_name;
  result.path = request.final_path;
  result.file_count = request.validated_files.size();
  result.filtered_count = request.filtered_count;
  result.validation_failures = request.failures;
  result.error = std::move(error);
  return result;
}

} // namespace

// clang-format off
EM_JS(void, megatoy_import_decide_js, (void *request, int proceed), {
  var common = Module.__megatoyFolderImportCommon;
  if (common) common.decide(request, !!proceed);
});

EM_JS(void, megatoy_import_sync_js, (void *request), {
  FS.syncfs(false, function(error) {
    var ptr = 0;
    if (error) ptr = stringToNewUTF8("" + error);
    Module["_megatoy_folder_import_sync_done"](request, error ? 0 : 1, ptr);
    if (ptr) _free(ptr);
  });
});

EM_JS(void, megatoy_import_settled_js, (void *request), {
  var common = Module.__megatoyFolderImportCommon;
  if (common) common.settle(request);
});
// clang-format on

namespace {

void dispatch_result(FolderImportResult result) {
  if (result.ok &&
      (result.filtered_count > 0 || !result.validation_failures.empty())) {
    g_result_reports.push_back(ResultReport{
        .folder_name = result.folder_name,
        .path = result.path,
        .imported_count = result.file_count,
        .filtered_count = result.filtered_count,
        .failures = result.validation_failures,
    });
  }

  if (g_active_import && g_active_import->is_drop) {
    if (g_drop_handler) {
      g_drop_handler(std::move(result));
    }
    return;
  }

  std::unique_ptr<PendingImport> pending;
  if (g_active_import) {
    pending.reset(static_cast<PendingImport *>(g_active_import->picker_handle));
  }
  if (pending && pending->on_complete) {
    pending->on_complete(std::move(result));
  }
}

void finish_active(bool ok, bool cancelled, std::string error) {
  if (!g_active_import) {
    return;
  }
  auto result = make_result(*g_active_import, ok, cancelled, std::move(error));
  g_close_import_popup = true;
  megatoy_import_settled_js(g_active_import.get());
  dispatch_result(std::move(result));
  if (ok) {
    request_storage_persist();
  }
  g_active_import.reset();
}

void abort_active(bool cancelled, std::string error) {
  if (!g_active_import) {
    return;
  }
  if (g_active_import->stager) {
    if (g_active_import->stager->committed()) {
      std::string rollback_error;
      if (!g_active_import->stager->rollback_commit(rollback_error) &&
          error.empty()) {
        error = std::move(rollback_error);
      }
    } else {
      g_active_import->stager->abort();
    }
  }
  finish_active(false, cancelled, std::move(error));
}

void finish_validation() {
  auto &request = *g_active_import;
  if (request.cancel_requested.load(std::memory_order_relaxed)) {
    abort_active(true, {});
    return;
  }

  std::string error;
  if (!request.stager->commit(request.validated_files, error)) {
    abort_active(false, std::move(error));
    return;
  }

  request.phase.store(ImportPhase::Syncing, std::memory_order_relaxed);
  request.current_files.store(request.validated_files.size(),
                              std::memory_order_relaxed);
  request.total_files.store(request.validated_files.size(),
                            std::memory_order_relaxed);
  megatoy_import_sync_js(&request);
}

void validate_next_file() {
  if (!g_active_import ||
      g_active_import->phase.load(std::memory_order_relaxed) !=
          ImportPhase::Validating) {
    return;
  }
  auto &request = *g_active_import;
  if (request.cancel_requested.load(std::memory_order_relaxed)) {
    abort_active(true, {});
    return;
  }
  if (request.validation_index >= request.staged_files.size()) {
    finish_validation();
    return;
  }

  const auto relative = request.staged_files[request.validation_index++];
  const auto result =
      import_pipeline::validate_file(request.staging_path / relative);
  if (result.valid) {
    request.validated_files.push_back(relative);
    if (result.warmed_container) {
      request.warmed_containers.emplace_back(relative, result.warmed_container);
    }
  } else {
    request.failures.push_back({relative, result.reason});
  }
  request.current_files.store(request.validation_index,
                              std::memory_order_relaxed);
  if (request.validation_index >= request.staged_files.size()) {
    finish_validation();
  }
}

// A fixed per-frame time budget instead of one file per frame: large imports
// are the whole point of this pipeline, and 60 validations/second would crawl.
void run_validation_slice() {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
  while (g_active_import &&
         g_active_import->phase.load(std::memory_order_relaxed) ==
             ImportPhase::Validating &&
         std::chrono::steady_clock::now() < deadline) {
    validate_next_file();
  }
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int
megatoy_folder_import_extension_supported(const char *extension) {
  return extension != nullptr && import_pipeline::supports_extension(extension);
}

EMSCRIPTEN_KEEPALIVE int
megatoy_folder_import_needs_confirmation(int file_count, double byte_count) {
  const auto count = file_count > 0 ? static_cast<std::size_t>(file_count) : 0;
  const auto bytes =
      byte_count > 0 ? static_cast<std::uint64_t>(byte_count) : 0;
  return import_pipeline::needs_confirmation(count, bytes);
}

EMSCRIPTEN_KEEPALIVE void *megatoy_folder_import_begin(
    void *picker_handle, int is_drop, const char *folder_name,
    const char *staging_path, const char *final_path, int kept_count,
    double kept_bytes, int filtered_count, double quota_remaining) {
  if (g_active_import) {
    return nullptr;
  }
  auto request = std::make_unique<ImportRequest>();
  request->picker_handle = picker_handle;
  request->is_drop = is_drop != 0;
  request->folder_name = folder_name != nullptr ? folder_name : "Imported";
  request->staging_path = staging_path != nullptr ? staging_path : "";
  request->final_path = final_path != nullptr ? final_path : "";
  request->kept_count =
      kept_count > 0 ? static_cast<std::size_t>(kept_count) : 0;
  request->kept_bytes =
      kept_bytes > 0 ? static_cast<std::uint64_t>(kept_bytes) : 0;
  request->filtered_count =
      filtered_count > 0 ? static_cast<std::size_t>(filtered_count) : 0;
  request->quota_remaining =
      quota_remaining >= 0 ? static_cast<std::int64_t>(quota_remaining) : -1;
  request->total_files.store(request->kept_count, std::memory_order_relaxed);
  request->total_bytes.store(request->kept_bytes, std::memory_order_relaxed);
  request->stager = std::make_unique<import_pipeline::ImportStager>(
      request->staging_path, request->final_path);
  if (!import_pipeline::needs_confirmation(request->kept_count,
                                           request->kept_bytes)) {
    request->phase.store(ImportPhase::Copying, std::memory_order_relaxed);
  }
  auto *raw = request.get();
  g_active_import = std::move(request);
  return raw;
}

EMSCRIPTEN_KEEPALIVE void megatoy_folder_import_progress(void *request_pointer,
                                                         int current_files,
                                                         int total_files,
                                                         double current_bytes,
                                                         double total_bytes) {
  if (!g_active_import || g_active_import.get() != request_pointer) {
    return;
  }
  auto &request = *g_active_import;
  request.current_files.store(
      current_files > 0 ? static_cast<std::size_t>(current_files) : 0,
      std::memory_order_relaxed);
  request.total_files.store(
      total_files > 0 ? static_cast<std::size_t>(total_files) : 0,
      std::memory_order_relaxed);
  request.current_bytes.store(
      current_bytes > 0 ? static_cast<std::uint64_t>(current_bytes) : 0,
      std::memory_order_relaxed);
  request.total_bytes.store(
      total_bytes > 0 ? static_cast<std::uint64_t>(total_bytes) : 0,
      std::memory_order_relaxed);
}

EMSCRIPTEN_KEEPALIVE int
megatoy_folder_import_cancel_requested(void *request_pointer) {
  return g_active_import && g_active_import.get() == request_pointer &&
         g_active_import->cancel_requested.load(std::memory_order_relaxed);
}

EMSCRIPTEN_KEEPALIVE void
megatoy_folder_import_stage_file(void *request_pointer,
                                 const char *relative_path) {
  if (!g_active_import || g_active_import.get() != request_pointer ||
      relative_path == nullptr) {
    return;
  }
  g_active_import->staged_files.emplace_back(relative_path);
}

EMSCRIPTEN_KEEPALIVE void megatoy_folder_import_copy_done(void *request_pointer,
                                                          int ok,
                                                          const char *error) {
  if (!g_active_import || g_active_import.get() != request_pointer) {
    return;
  }
  if (!ok) {
    const bool cancelled =
        g_active_import->cancel_requested.load(std::memory_order_relaxed);
    abort_active(cancelled, cancelled
                                ? std::string{}
                                : (error != nullptr ? error : "copy failed"));
    return;
  }
  g_active_import->phase.store(ImportPhase::Validating,
                               std::memory_order_relaxed);
  g_active_import->current_files.store(0, std::memory_order_relaxed);
  g_active_import->total_files.store(g_active_import->staged_files.size(),
                                     std::memory_order_relaxed);
}

EMSCRIPTEN_KEEPALIVE void megatoy_folder_import_sync_done(void *request_pointer,
                                                          int ok,
                                                          const char *error) {
  if (!g_active_import || g_active_import.get() != request_pointer) {
    return;
  }
  if (!ok ||
      g_active_import->cancel_requested.load(std::memory_order_relaxed)) {
    const bool cancelled =
        g_active_import->cancel_requested.load(std::memory_order_relaxed);
    abort_active(cancelled,
                 cancelled
                     ? std::string{}
                     : (error != nullptr ? error : "storage sync failed"));
    return;
  }
  for (auto &[relative, warmed] : g_active_import->warmed_containers) {
    import_pipeline::store_warmed_container(
        g_active_import->final_path / relative, std::move(warmed));
  }
  finish_active(true, false, {});
}

EMSCRIPTEN_KEEPALIVE void
megatoy_folder_import_early_result(void *picker_handle, int is_drop,
                                   int cancelled, const char *folder_name,
                                   const char *error) {
  FolderImportResult result;
  result.cancelled = cancelled != 0;
  result.folder_name = folder_name != nullptr ? folder_name : "";
  result.error = error != nullptr ? error : "";
  if (is_drop) {
    if (g_drop_handler) {
      g_drop_handler(std::move(result));
    }
    return;
  }
  std::unique_ptr<PendingImport> pending(
      static_cast<PendingImport *>(picker_handle));
  if (pending && pending->on_complete) {
    pending->on_complete(std::move(result));
  }
}

} // extern "C"

// clang-format off
EM_JS(void, megatoy_install_folder_import_common_js, (), {
  if (Module.__megatoyFolderImportCommon) return;

  var decisions = new Map();
  var settlements = new Map();
  var uniqueCounter = 1;
  var YIELD_BYTES = 256 * 1024;
  var QUOTA_SAFETY_FRACTION = 0.10;
  var QUOTA_SAFETY_MINIMUM = 16 * 1024 * 1024;

  function mkdirp(dir) {
    var parts = dir.split("/").filter(function(part) { return part.length > 0; });
    var built = "";
    for (var i = 0; i < parts.length; i++) {
      built += "/" + parts[i];
      if (!FS.analyzePath(built).exists) {
        try { FS.mkdir(built); } catch (error) {
          if (!FS.analyzePath(built).exists) throw error;
        }
      }
    }
  }

  function safeRelative(path) {
    var normalized = "";
    path = path || "";
    for (var character = 0; character < path.length; character++) {
      normalized += path.charCodeAt(character) === 92 ? "/" : path[character];
    }
    var parts = normalized.split("/");
    var safe = [];
    for (var i = 0; i < parts.length; i++) {
      if (!parts[i] || parts[i] === ".") continue;
      if (parts[i] === "..") throw new Error("Unsafe relative path");
      safe.push(parts[i]);
    }
    return safe.join("/");
  }

  function supported(file) {
    var name = file.name || "";
    var dot = name.lastIndexOf(".");
    var extension = dot >= 0 ? name.substring(dot).toLowerCase() : "";
    var ptr = stringToNewUTF8(extension);
    var result = Module["_megatoy_folder_import_extension_supported"](ptr);
    _free(ptr);
    return !!result;
  }

  function earlyResult(handle, isDrop, cancelled, folderName, error) {
    var namePtr = stringToNewUTF8(folderName || "");
    var errorPtr = stringToNewUTF8(error || "");
    Module["_megatoy_folder_import_early_result"](
        handle || 0, isDrop ? 1 : 0, cancelled ? 1 : 0, namePtr, errorPtr);
    _free(namePtr); _free(errorPtr);
  }

  async function estimateStorage(bytes) {
    if (!Module.__megatoyImportPersistRequested) {
      Module.__megatoyImportPersistRequested = true;
      if (navigator.storage && navigator.storage.persist) {
        navigator.storage.persist().catch(function() {});
      }
    }
    if (!navigator.storage || !navigator.storage.estimate) return -1;
    try {
      var estimate = await navigator.storage.estimate();
      if (typeof estimate.quota !== "number" ||
          typeof estimate.usage !== "number") return -1;
      var remaining = Math.max(0, estimate.quota - estimate.usage);
      var margin = Math.max(QUOTA_SAFETY_MINIMUM,
                            estimate.quota * QUOTA_SAFETY_FRACTION);
      if (bytes > Math.max(0, remaining - margin)) {
        throw new Error("Not enough browser storage: this import needs " +
                        (bytes / 1048576).toFixed(1) + " MB, but only " +
                        (remaining / 1048576).toFixed(1) +
                        " MB remains before the safety reserve.");
      }
      return remaining;
    } catch (error) {
      if (("" + error).indexOf("Not enough browser storage") >= 0) throw error;
      return -1;
    }
  }

  function waitForDecision(request) {
    return new Promise(function(resolve) { decisions.set(request, resolve); });
  }
  function waitForSettlement(request) {
    return new Promise(function(resolve) { settlements.set(request, resolve); });
  }

  async function writeFileChunked(file, destination, request, progress) {
    var stream = FS.open(destination, "w");
    try {
      var offset = 0;
      while (offset < file.size) {
        if (Module["_megatoy_folder_import_cancel_requested"](request)) {
          throw new Error("Import cancelled");
        }
        var end = Math.min(offset + YIELD_BYTES, file.size);
        var bytes = new Uint8Array(await file.slice(offset, end).arrayBuffer());
        FS.write(stream, bytes, 0, bytes.length, offset);
        offset = end;
        progress.bytes += bytes.length;
        Module["_megatoy_folder_import_progress"](
            request, progress.files, progress.totalFiles,
            progress.bytes, progress.totalBytes);
        await new Promise(function(resolve) { setTimeout(resolve, 0); });
      }
    } finally {
      FS.close(stream);
    }
  }

  async function run(candidate, destination, handle, isDrop) {
    var kept = [];
    var filtered = 0;
    var totalBytes = 0;
    for (var i = 0; i < candidate.files.length; i++) {
      var item = candidate.files[i];
      if (supported(item.file)) {
        item.relative = safeRelative(item.relative);
        if (!item.relative) continue;
        kept.push(item);
        totalBytes += item.file.size || 0;
      } else {
        filtered++;
      }
    }
    if (!kept.length) {
      earlyResult(handle, isDrop, false, candidate.name,
                  "No supported instrument files were found (" +
                  filtered + " unrelated files skipped).");
      return;
    }

    var quotaRemaining;
    try {
      quotaRemaining = await estimateStorage(totalBytes);
    } catch (error) {
      earlyResult(handle, isDrop, false, candidate.name, "" + error);
      return;
    }

    var originalName = candidate.name || "Imported";
    var safeName = "";
    for (var nameIndex = 0; nameIndex < originalName.length; nameIndex++) {
      var code = originalName.charCodeAt(nameIndex);
      safeName += code === 47 || code === 92 ? "_" : originalName[nameIndex];
    }
    if (!safeName || safeName === "." || safeName === "..") safeName = "Imported";
    var target = destination + "/" + safeName;
    var finalPath = target;
    for (var suffix = 2; FS.analyzePath(finalPath).exists; suffix++) {
      finalPath = target + " (" + suffix + ")";
    }
    var stagingPath = destination + "/.import-tmp-" + Date.now() + "-" +
                      (uniqueCounter++);

    var namePtr = stringToNewUTF8(safeName);
    var stagePtr = stringToNewUTF8(stagingPath);
    var finalPtr = stringToNewUTF8(finalPath);
    var request = Module["_megatoy_folder_import_begin"](
        handle || 0, isDrop ? 1 : 0, namePtr, stagePtr, finalPtr,
        kept.length, totalBytes, filtered, quotaRemaining);
    _free(namePtr); _free(stagePtr); _free(finalPtr);
    if (!request) {
      earlyResult(handle, isDrop, false, safeName,
                  "Another folder import is already in progress.");
      return;
    }

    var settled = waitForSettlement(request);
    var needsConfirmation = Module["_megatoy_folder_import_needs_confirmation"](
        kept.length, totalBytes);
    if (needsConfirmation && !(await waitForDecision(request))) {
      await settled;
      return;
    }

    var progress = {files: 0, totalFiles: kept.length,
                    bytes: 0, totalBytes: totalBytes};
    try {
      mkdirp(stagingPath);
      for (var index = 0; index < kept.length; index++) {
        if (Module["_megatoy_folder_import_cancel_requested"](request)) {
          throw new Error("Import cancelled");
        }
        var item = kept[index];
        var fullPath = stagingPath + "/" + item.relative;
        mkdirp(fullPath.substring(0, fullPath.lastIndexOf("/")));
        await writeFileChunked(item.file, fullPath, request, progress);
        progress.files++;
        var relativePtr = stringToNewUTF8(item.relative);
        Module["_megatoy_folder_import_stage_file"](request, relativePtr);
        _free(relativePtr);
        Module["_megatoy_folder_import_progress"](
            request, progress.files, progress.totalFiles,
            progress.bytes, progress.totalBytes);
      }
    } catch (error) {
      var cancelled = Module["_megatoy_folder_import_cancel_requested"](request);
      var errorPtr = stringToNewUTF8(cancelled ? "" : ("" + error));
      Module["_megatoy_folder_import_copy_done"](request, 0, errorPtr);
      _free(errorPtr);
      await settled;
      return;
    }
    Module["_megatoy_folder_import_copy_done"](request, 1, 0);
    await settled;
  }

  function readAll(reader) {
    return new Promise(function(resolve, reject) {
      var all = [];
      (function next() {
        reader.readEntries(function(batch) {
          if (!batch.length) { resolve(all); return; }
          all = all.concat(Array.prototype.slice.call(batch));
          next();
        }, reject);
      })();
    });
  }

  async function enumerateEntry(entry, prefix, output) {
    var relative = prefix ? prefix + "/" + entry.name : entry.name;
    if (entry.isDirectory) {
      var children = await readAll(entry.createReader());
      for (var i = 0; i < children.length; i++) {
        await enumerateEntry(children[i], relative, output);
      }
      return;
    }
    var file = await new Promise(function(resolve, reject) {
      entry.file(resolve, reject);
    });
    output.push({file: file, relative: relative});
  }

  Module.__megatoyFolderImportCommon = {
    run: run,
    enumerateEntry: enumerateEntry,
    decide: function(request, proceed) {
      var resolve = decisions.get(request);
      decisions.delete(request);
      if (resolve) resolve(proceed);
    },
    settle: function(request) {
      var resolve = settlements.get(request);
      settlements.delete(request);
      decisions.delete(request);
      if (resolve) resolve();
    },
    earlyResult: earlyResult
  };
});

EM_JS(void, megatoy_open_folder_import, (const char *destination,
                                         void *handle), {
  var destinationRoot = UTF8ToString(destination);
  var common = Module.__megatoyFolderImportCommon;
  var input = document.createElement("input");
  input.type = "file";
  input.webkitdirectory = true;
  input.multiple = true;
  input.style.display = "none";
  document.body.appendChild(input);
  var settled = false;
  function cancel() {
    if (settled) return;
    settled = true;
    input.remove();
    common.earlyResult(handle, false, true, "", "");
  }
  input.addEventListener("cancel", cancel);
  input.addEventListener("change", async function() {
    if (settled) return;
    var files = Array.from(input.files || []);
    if (!files.length) { cancel(); return; }
    settled = true;
    var first = files[0].webkitRelativePath || files[0].name;
    var folderName = first.split("/")[0] || "Imported";
    var items = files.map(function(file) {
      var path = file.webkitRelativePath || file.name;
      var slash = path.indexOf("/");
      return {file: file, relative: slash >= 0 ? path.substring(slash + 1) : path};
    });
    await common.run({name: folderName, files: items}, destinationRoot,
                     handle, false);
    input.remove();
  });
  input.click();
});

EM_JS(void, megatoy_install_drop_import_js, (const char *destination), {
  if (Module.__megatoyDropImport) return;
  Module.__megatoyDropImport = true;
  var destinationRoot = UTF8ToString(destination);
  var common = Module.__megatoyFolderImportCommon;

  document.addEventListener("drop", function(event) {
    var items = event.dataTransfer && event.dataTransfer.items;
    if (!items) return;
    var directories = [];
    for (var i = 0; i < items.length; i++) {
      var entry = items[i].webkitGetAsEntry && items[i].webkitGetAsEntry();
      if (entry && entry.isDirectory) directories.push(entry);
    }
    if (!directories.length) return;
    event.preventDefault();
    event.stopPropagation();

    (async function() {
      for (var index = 0; index < directories.length; index++) {
        var directory = directories[index];
        var files = [];
        try {
          var children = await (new Promise(function(resolve, reject) {
            var reader = directory.createReader();
            var all = [];
            (function next() {
              reader.readEntries(function(batch) {
                if (!batch.length) { resolve(all); return; }
                all = all.concat(Array.prototype.slice.call(batch)); next();
              }, reject);
            })();
          }));
          for (var child = 0; child < children.length; child++) {
            await common.enumerateEntry(children[child], "", files);
          }
          await common.run({name: directory.name, files: files},
                           destinationRoot, 0, true);
        } catch (error) {
          common.earlyResult(0, true, false, directory.name, "" + error);
        }
      }
    })();
  }, true);
});
// clang-format on

void render_folder_import_ui() {
  run_validation_slice();

  if (!g_active_import && g_close_import_popup) {
    const char *title = ImGui::IsPopupOpen("Importing Folder")
                            ? "Importing Folder"
                            : "Import Folder";
    if (ImGui::IsPopupOpen(title) &&
        ImGui::BeginPopupModal(title, nullptr,
                               ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    g_close_import_popup = false;
  }

  if (g_active_import) {
    auto &request = *g_active_import;
    const auto phase = request.phase.load(std::memory_order_relaxed);
    if (phase == ImportPhase::AwaitingConfirmation) {
      // Open on the edge only: re-opening an already-open popup every frame
      // resets popup state and can swallow clicks (see confirmation_dialog).
      if (!ImGui::IsPopupOpen("Import Folder")) {
        ImGui::OpenPopup("Import Folder");
      }
      ui::center_next_window();
      if (ImGui::BeginPopupModal("Import Folder", nullptr,
                                 ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::force_center_window();
        ImGui::TextWrapped("Import %zu files (%s MB) from \"%s\"?",
                           request.kept_count,
                           format_megabytes(request.kept_bytes).c_str(),
                           request.folder_name.c_str());
        if (request.filtered_count > 0) {
          ImGui::TextDisabled("%zu unrelated files skipped",
                              request.filtered_count);
        }
        if (request.quota_remaining >= 0) {
          ImGui::TextDisabled("Browser storage remaining: %s MB",
                              format_megabytes(static_cast<std::uint64_t>(
                                                   request.quota_remaining))
                                  .c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
          megatoy_import_decide_js(&request, 0);
          ImGui::CloseCurrentPopup();
          abort_active(true, {});
        }
        ImGui::SameLine();
        if (ImGui::Button("Import", ImVec2(120, 0))) {
          request.phase.store(ImportPhase::Copying, std::memory_order_relaxed);
          megatoy_import_decide_js(&request, 1);
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    } else {
      if (!ImGui::IsPopupOpen("Importing Folder")) {
        ImGui::OpenPopup("Importing Folder");
      }
      ui::center_next_window();
      if (ImGui::BeginPopupModal("Importing Folder", nullptr,
                                 ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::force_center_window();
        const auto current =
            request.current_files.load(std::memory_order_relaxed);
        const auto total = request.total_files.load(std::memory_order_relaxed);
        const char *label = phase == ImportPhase::Copying      ? "Copying"
                            : phase == ImportPhase::Validating ? "Validating"
                                                               : "Saving";
        if (request.cancel_requested.load(std::memory_order_relaxed)) {
          ImGui::TextUnformatted("Cancelling...");
        } else {
          ImGui::Text("%s %zu/%zu...", label, current, total);
        }
        float fraction = total == 0 ? 0.0f
                                    : static_cast<float>(current) /
                                          static_cast<float>(total);
        if (phase == ImportPhase::Copying) {
          const auto current_bytes =
              request.current_bytes.load(std::memory_order_relaxed);
          const auto total_bytes =
              request.total_bytes.load(std::memory_order_relaxed);
          if (total_bytes > 0) {
            fraction = static_cast<float>(current_bytes) /
                       static_cast<float>(total_bytes);
          }
        }
        ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(360, 0));
        ImGui::Spacing();
        ImGui::BeginDisabled(
            request.cancel_requested.load(std::memory_order_relaxed));
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
          request.cancel_requested.store(true, std::memory_order_relaxed);
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
      }
    }
  }

  if (!g_result_reports.empty() && g_result_reports.front().open &&
      !g_close_import_popup) {
    ImGui::OpenPopup("Folder Import Complete");
    g_result_reports.front().open = false;
  }
  if (!g_result_reports.empty()) {
    auto &report = g_result_reports.front();
    ui::center_next_window();
    if (ImGui::BeginPopupModal("Folder Import Complete", nullptr,
                               ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize)) {
      ui::force_center_window();
      ImGui::TextWrapped("Imported %zu files into %s.", report.imported_count,
                         report.folder_name.c_str());
      if (report.filtered_count > 0) {
        ImGui::Text("%zu files skipped (unsupported type)",
                    report.filtered_count);
      }
      if (!report.failures.empty()) {
        ImGui::Text("%zu files failed validation:", report.failures.size());
        ImGui::BeginChild("validation_failures", ImVec2(520, 180), true);
        for (const auto &failure : report.failures) {
          // ASCII separator: the bundled font has no em-dash glyph.
          ImGui::BulletText("%s - %s",
                            failure.relative_path.generic_string().c_str(),
                            failure.reason.c_str());
        }
        ImGui::EndChild();
      }
      ImGui::Spacing();
      if (ImGui::Button("OK", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
        g_result_reports.pop_front();
      }
      ImGui::EndPopup();
    }
  }
}

void set_drop_import_handler(std::function<void(FolderImportResult)> handler) {
  g_drop_handler = std::move(handler);
}

void install_drop_import(const std::filesystem::path &destination_root) {
  megatoy_install_folder_import_common_js();
  megatoy_install_drop_import_js(destination_root.string().c_str());
}

void import_folder(const std::filesystem::path &destination_root,
                   std::function<void(FolderImportResult)> on_complete) {
  megatoy_install_folder_import_common_js();
  auto *pending = new PendingImport{std::move(on_complete)};
  megatoy_open_folder_import(destination_root.string().c_str(), pending);
}

} // namespace platform::web

#endif
