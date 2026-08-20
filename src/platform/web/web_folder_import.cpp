#include "platform/web/web_folder_import.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "core/status.hpp"
#include "gui/components/common.hpp"
#include "platform/clipboard.hpp"
#include "platform/import_pipeline.hpp"
#include "platform/web/web_storage_persistence.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
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

/**
 * The stretch between "Import Folder..." was clicked and the browser handing
 * us a file list, which for a large tree is minutes of total silence: Chrome
 * enumerates the whole directory before it even shows its own confirmation
 * dialog, and nothing reaches the page until then. Without this the app looks
 * frozen and the user reloads -- which leaves the chooser flow pending and
 * makes the next click do nothing at all.
 */
enum class SelectionPhase : int {
  WaitingForPicker = 0,
  Scanning = 1,
};

struct PendingSelection {
  /// Identity handed to JavaScript. Deliberately NOT the slot address: a
  /// cancelled selection frees the slot, and the next selection can land on
  /// the very same heap address, so a stale JS callback would mistake the new
  /// flow for its own. A monotonic token can never be reused, and nothing
  /// ever dereferences it.
  void *token = nullptr;
  bool is_drop = false;
  std::atomic<SelectionPhase> phase{SelectionPhase::WaitingForPicker};
  std::atomic<std::uint64_t> scanned{0};
  /// 0 while unknown -- drag-drop enumeration has no count until it finishes.
  std::atomic<std::uint64_t> total{0};
  std::atomic<bool> abandoned{false};
  /// Cancelled while the browser still owned the flow. The chooser pipeline is
  /// a single browser-side resource that a page can neither observe nor stop:
  /// freeing the slot here would let another picker be clicked, and the
  /// browser queues those clicks and replays every one of them when the
  /// pending flow finally resolves (observed: N stacked file dialogs). So the
  /// slot stays held -- invisibly -- until the change/cancel event arrives.
  std::atomic<bool> hidden{false};
};

std::function<void(FolderImportResult)> g_drop_handler;
std::unique_ptr<ImportRequest> g_active_import;
std::unique_ptr<PendingSelection> g_pending_selection;
std::uintptr_t g_next_selection_token = 1;
std::deque<ResultReport> g_result_reports;
bool g_close_import_popup = false;
bool g_close_selection_popup = false;

constexpr const char *kSelectionPopupId = "Import Folder###ImportSelection";

/// Null when a selection or an import is already running; the caller reports
/// that to the user rather than opening a second picker behind the first.
void *begin_pending_selection(bool is_drop) {
  if (g_pending_selection || g_active_import) {
    return nullptr;
  }
  auto selection = std::make_unique<PendingSelection>();
  selection->token = reinterpret_cast<void *>(g_next_selection_token++);
  selection->is_drop = is_drop;
  auto *token = selection->token;
  g_pending_selection = std::move(selection);
  return token;
}

void end_pending_selection() {
  if (!g_pending_selection) {
    return;
  }
  g_pending_selection.reset();
  g_close_selection_popup = true;
}

bool selection_matches(void *token) {
  return token != nullptr && g_pending_selection &&
         g_pending_selection->token == token;
}

void notify_import_busy() {
  if (g_pending_selection &&
      g_pending_selection->phase.load(std::memory_order_relaxed) ==
          SelectionPhase::WaitingForPicker) {
    megatoy::status::info("Still reading the folder selection.");
    return;
  }
  megatoy::status::warning("A folder import is already in progress.");
}

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

/**
 * The whole supported-extension set as one comma-joined lowercase string, so
 * JavaScript can build a Set once and filter a million-entry selection with
 * pure JS lookups. The old per-file wasm roundtrip (a string allocation plus
 * a call each) froze the tab for minutes on a large folder.
 */
EMSCRIPTEN_KEEPALIVE const char *megatoy_folder_import_supported_extensions() {
  static const std::string joined = [] {
    std::string result;
    for (const auto &extension : import_pipeline::supported_extensions()) {
      if (!result.empty()) {
        result.push_back(',');
      }
      for (const char character : extension) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))));
      }
    }
    return result;
  }();
  return joined.c_str();
}

EMSCRIPTEN_KEEPALIVE void megatoy_folder_import_busy_notice() {
  notify_import_busy();
}

EMSCRIPTEN_KEEPALIVE void *megatoy_folder_import_selection_begin(int is_drop) {
  return begin_pending_selection(is_drop != 0);
}

EMSCRIPTEN_KEEPALIVE void
megatoy_folder_import_selection_started(void *selection, double total) {
  if (!selection_matches(selection)) {
    return;
  }
  g_pending_selection->total.store(total > 0 ? static_cast<std::uint64_t>(total)
                                             : 0,
                                   std::memory_order_relaxed);
  g_pending_selection->scanned.store(0, std::memory_order_relaxed);
  g_pending_selection->phase.store(SelectionPhase::Scanning,
                                   std::memory_order_relaxed);
  // A dialog closed during the wait comes back once there is progress to
  // show -- closing does not stop the import.
  g_pending_selection->hidden.store(false, std::memory_order_relaxed);
}

EMSCRIPTEN_KEEPALIVE void
megatoy_folder_import_selection_progress(void *selection, double scanned) {
  if (!selection_matches(selection)) {
    return;
  }
  g_pending_selection->scanned.store(
      scanned > 0 ? static_cast<std::uint64_t>(scanned) : 0,
      std::memory_order_relaxed);
}

/// An unknown token means the slot is gone -- the user pressed Cancel -- so
/// the still-running JavaScript must treat it as abandoned and unwind.
EMSCRIPTEN_KEEPALIVE int
megatoy_folder_import_selection_abandoned(void *selection) {
  if (!selection_matches(selection)) {
    return 1;
  }
  return g_pending_selection->abandoned.load(std::memory_order_relaxed) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void megatoy_folder_import_selection_end(void *selection) {
  if (!selection_matches(selection)) {
    return;
  }
  end_pending_selection();
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
  // Filtering a million entries must never hold the main thread: work in
  // chunks and yield, so the progress modal keeps painting and Cancel works.
  var FILTER_CHUNK = 20000;
  var ENUMERATE_PROGRESS_INTERVAL = 500;

  // Fetched once; the per-file alternative was two wasm calls per file.
  var extensionSet = new Set();
  (function() {
    var pointer = Module["_megatoy_folder_import_supported_extensions"]();
    var joined = pointer ? UTF8ToString(pointer) : "";
    var parts = joined.split(",");
    for (var i = 0; i < parts.length; i++) {
      if (parts[i]) extensionSet.add(parts[i]);
    }
  })();

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
    var name = (file && file.name) || "";
    var dot = name.lastIndexOf(".");
    if (dot < 0) return false;
    return extensionSet.has(name.substring(dot).toLowerCase());
  }

  function selectionStarted(selection, total) {
    if (selection) {
      Module["_megatoy_folder_import_selection_started"](selection, total);
    }
  }
  function selectionProgress(selection, scanned) {
    if (selection) {
      Module["_megatoy_folder_import_selection_progress"](selection, scanned);
    }
  }
  function selectionAbandoned(selection) {
    return selection
        ? !!Module["_megatoy_folder_import_selection_abandoned"](selection)
        : false;
  }
  function selectionEnd(selection) {
    if (selection) {
      Module["_megatoy_folder_import_selection_end"](selection);
    }
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

  // Filters -- and, for the picker, *builds* -- the item list in chunks with a
  // yield between them. `candidate.files` is either an array of
  // {file, relative} (drop) or the picker's raw FileList; mapping a FileList
  // of 1.7M entries into an array up front is itself a multi-second freeze,
  // so that mapping happens here, inside the chunked pass.
  // Returns null when the user abandoned the selection.
  async function scanCandidate(candidate, selection) {
    var source = candidate.files;
    var count = source.length;
    var fromFileList = !!candidate.fromFileList;
    var kept = [];
    var filtered = 0;
    var totalBytes = 0;
    for (var i = 0; i < count; i++) {
      if (i > 0 && (i % FILTER_CHUNK) === 0) {
        selectionProgress(selection, i);
        if (selectionAbandoned(selection)) return null;
        await new Promise(function(resolve) { setTimeout(resolve, 0); });
      }
      var file;
      var relative;
      if (fromFileList) {
        file = source[i];
        var path = file.webkitRelativePath || file.name;
        var slash = path.indexOf("/");
        relative = slash >= 0 ? path.substring(slash + 1) : path;
      } else {
        file = source[i].file;
        relative = source[i].relative;
      }
      if (!supported(file)) { filtered++; continue; }
      relative = safeRelative(relative);
      if (!relative) continue;
      kept.push({file: file, relative: relative});
      totalBytes += file.size || 0;
    }
    selectionProgress(selection, count);
    if (selectionAbandoned(selection)) return null;
    return {kept: kept, filtered: filtered, totalBytes: totalBytes};
  }

  async function run(candidate, destination, handle, isDrop, selection) {
    var scan;
    try {
      scan = await scanCandidate(candidate, selection);
    } catch (error) {
      selectionEnd(selection);
      earlyResult(handle, isDrop, false, candidate.name, "" + error);
      return;
    }
    if (!scan) {
      // Cancelled from the modal: unwind quietly, freeing the C++ handle.
      // The flag lets a multi-directory drop abandon the whole batch.
      candidate.abandoned = true;
      selectionEnd(selection);
      earlyResult(handle, isDrop, true, "", "");
      return;
    }
    var kept = scan.kept;
    var filtered = scan.filtered;
    var totalBytes = scan.totalBytes;
    if (!kept.length) {
      selectionEnd(selection);
      earlyResult(handle, isDrop, false, candidate.name,
                  "No supported instrument files were found (" +
                  filtered + " unrelated files skipped).");
      return;
    }

    var quotaRemaining;
    try {
      quotaRemaining = await estimateStorage(totalBytes);
    } catch (error) {
      selectionEnd(selection);
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

    // The active-import machinery owns the progress UI from here on.
    selectionEnd(selection);

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

  // `counter` (optional) carries the pending-selection token so a dropped
  // tree reports progress while it is walked -- the count is unknown until
  // the walk ends -- and stops early when the user cancels.
  async function enumerateEntry(entry, prefix, output, counter) {
    if (counter && counter.abandoned) return;
    var relative = prefix ? prefix + "/" + entry.name : entry.name;
    if (entry.isDirectory) {
      var children = await readAll(entry.createReader());
      for (var i = 0; i < children.length; i++) {
        if (counter && counter.abandoned) return;
        await enumerateEntry(children[i], relative, output, counter);
      }
      return;
    }
    var file = await new Promise(function(resolve, reject) {
      entry.file(resolve, reject);
    });
    output.push({file: file, relative: relative});
    if (counter) {
      counter.scanned++;
      if ((counter.scanned % ENUMERATE_PROGRESS_INTERVAL) === 0) {
        selectionProgress(counter.selection, counter.scanned);
        if (selectionAbandoned(counter.selection)) counter.abandoned = true;
      }
    }
  }

  Module.__megatoyFolderImportCommon = {
    run: run,
    enumerateEntry: enumerateEntry,
    selectionStarted: selectionStarted,
    selectionProgress: selectionProgress,
    selectionAbandoned: selectionAbandoned,
    selectionEnd: selectionEnd,
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
                                         void *handle, void *selection), {
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
    common.selectionEnd(selection);
    common.earlyResult(handle, false, true, "", "");
  }
  input.addEventListener("cancel", cancel);
  input.addEventListener("change", async function() {
    if (settled) return;
    // The user may have given up during the browser's own (minutes-long)
    // enumeration; the slot is gone, so unwind without a notification.
    if (common.selectionAbandoned(selection)) {
      settled = true;
      input.remove();
      common.selectionEnd(selection);
      common.earlyResult(handle, false, true, "", "");
      return;
    }
    // Not Array.from(): copying a million-entry FileList is itself a freeze.
    var files = input.files || [];
    if (!files.length) { cancel(); return; }
    settled = true;
    common.selectionStarted(selection, files.length);
    var first = files[0].webkitRelativePath || files[0].name;
    var folderName = first.split("/")[0] || "Imported";
    await common.run({name: folderName, files: files, fromFileList: true},
                     destinationRoot, handle, false, selection);
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
        // One slot per directory: run() hands its slot to the active import,
        // and the loop only advances once that import has settled.
        var selection = Module["_megatoy_folder_import_selection_begin"](1);
        if (!selection) {
          Module["_megatoy_folder_import_busy_notice"]();
          return;
        }
        // No count to show until the walk finishes: report as we go.
        common.selectionStarted(selection, 0);
        var counter = {selection: selection, scanned: 0, abandoned: false};
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
            if (counter.abandoned) break;
            await common.enumerateEntry(children[child], "", files, counter);
          }
          if (counter.abandoned || common.selectionAbandoned(selection)) {
            common.selectionEnd(selection);
            common.earlyResult(0, true, true, "", "");
            return;
          }
          common.selectionStarted(selection, files.length);
          var candidate = {name: directory.name, files: files};
          await common.run(candidate, destinationRoot, 0, true, selection);
          if (candidate.abandoned) return;
        } catch (error) {
          common.selectionEnd(selection);
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

  if (!g_pending_selection && g_close_selection_popup) {
    if (ImGui::IsPopupOpen(kSelectionPopupId) &&
        ImGui::BeginPopupModal(kSelectionPopupId, nullptr,
                               ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    g_close_selection_popup = false;
  }

  // The active-import modals take over the moment the selection resolves; by
  // construction the two never coexist, but the guard keeps it that way. A
  // hidden slot (cancelled while the browser owned the flow) renders nothing.
  if (g_pending_selection && !g_active_import &&
      !g_pending_selection->hidden.load(std::memory_order_relaxed)) {
    auto &selection = *g_pending_selection;
    // Open on the edge only, as with the import modals below.
    if (!ImGui::IsPopupOpen(kSelectionPopupId)) {
      ImGui::OpenPopup(kSelectionPopupId);
    }
    ui::center_next_window();
    if (ImGui::BeginPopupModal(kSelectionPopupId, nullptr,
                               ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
      ui::force_center_window();
      const auto phase = selection.phase.load(std::memory_order_relaxed);
      if (phase == SelectionPhase::WaitingForPicker) {
        ImGui::TextUnformatted("Choose a folder to import.");
        ImGui::TextDisabled("Large folders can take the browser a while to "
                            "read after you choose.");
        ImGui::TextDisabled(
            "You can close this dialog; the import will continue.");
        ImGui::ProgressBar(static_cast<float>(-1.0 * ImGui::GetTime()),
                           ImVec2(360, 0));
      } else {
        const auto scanned = static_cast<unsigned long long>(
            selection.scanned.load(std::memory_order_relaxed));
        const auto total = static_cast<unsigned long long>(
            selection.total.load(std::memory_order_relaxed));
        if (total == 0) {
          if (selection.is_drop) {
            ImGui::Text("Scanning dropped folder: %llu files...", scanned);
          } else {
            ImGui::Text("Scanning selection: %llu files...", scanned);
          }
          ImGui::ProgressBar(static_cast<float>(-1.0 * ImGui::GetTime()),
                             ImVec2(360, 0));
        } else {
          ImGui::Text("Scanning selection: %llu / %llu files...", scanned,
                      total);
          ImGui::ProgressBar(std::clamp(static_cast<float>(scanned) /
                                            static_cast<float>(total),
                                        0.0f, 1.0f),
                             ImVec2(360, 0));
        }
      }
      ImGui::Spacing();
      if (phase == SelectionPhase::WaitingForPicker) {
        // The browser owns the chooser flow and cannot be stopped from here,
        // so this button only dismisses the dialog. The slot stays held --
        // hidden -- which keeps a second picker from being queued behind the
        // pending one, and the dialog returns when the files arrive. To
        // abort, the user cancels the browser's own dialog, whose cancel
        // event frees the slot.
        if (ImGui::Button("Close", ImVec2(120, 0))) {
          ImGui::CloseCurrentPopup();
          selection.hidden.store(true, std::memory_order_relaxed);
        }
      } else if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        // The files are already ours; destroying the slot is enough. Any
        // JavaScript still holding the token sees an unknown token, reads
        // it as abandoned and unwinds silently.
        selection.abandoned.store(true, std::memory_order_relaxed);
        ImGui::CloseCurrentPopup();
        end_pending_selection();
      }
      ImGui::EndPopup();
    }
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
    // AlwaysAutoResize keeps the dialog hugging its content; without it the
    // window inherits whatever size the ini remembered.
    if (ImGui::BeginPopupModal("Folder Import Complete", nullptr,
                               ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
      ui::force_center_window();
      ImGui::TextWrapped("Imported %zu files into %s.", report.imported_count,
                         report.folder_name.c_str());
      if (report.filtered_count > 0) {
        ImGui::Text("%zu files skipped (unsupported type)",
                    report.filtered_count);
      }
      if (!report.failures.empty()) {
        ImGui::Text("%zu files failed validation:", report.failures.size());
        // The list hugs its rows up to a cap, then scrolls.
        const float row_height = ImGui::GetTextLineHeightWithSpacing();
        const float list_height = std::min(
            200.0f,
            row_height * (static_cast<float>(report.failures.size()) + 0.5f) +
                ImGui::GetStyle().WindowPadding.y * 2.0f);
        ImGui::BeginChild("validation_failures", ImVec2(640, list_height),
                          true);
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
      ImGui::SameLine();
      if (ImGui::Button("Copy List", ImVec2(120, 0))) {
        std::string text = "Imported " + std::to_string(report.imported_count) +
                           " files into " + report.folder_name + ".\n";
        if (report.filtered_count > 0) {
          text += std::to_string(report.filtered_count) +
                  " files skipped (unsupported type)\n";
        }
        if (!report.failures.empty()) {
          text += std::to_string(report.failures.size()) +
                  " files failed validation:\n";
          for (const auto &failure : report.failures) {
            text += failure.relative_path.generic_string() + " - " +
                    failure.reason + "\n";
          }
        }
        platform::clipboard::copy_text(text);
        megatoy::status::success("Copied the import report.");
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
  // A second picker behind a pending one is what made the original incident
  // unrecoverable: the click looked like it did nothing at all.
  auto *selection = begin_pending_selection(false);
  if (selection == nullptr) {
    notify_import_busy();
    return;
  }
  megatoy_install_folder_import_common_js();
  auto *pending = new PendingImport{std::move(on_complete)};
  megatoy_open_folder_import(destination_root.string().c_str(), pending,
                             selection);
}

} // namespace platform::web

#endif
