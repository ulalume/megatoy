#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace patches {

/**
 * One patch's ratings, icon and notes, as read from its sidecar. A field
 * megatoy does not model, or a modelled one holding the wrong type, is kept
 * in `extra` and written back unchanged.
 */
struct PatchMetadata {
  std::string hash;    ///< Hash of the patch content.
  int star_rating = 0; ///< 0-5.
  std::string icon;    ///< Icon id, such as "organ".

  std::vector<std::string> tags;
  std::string notes;

  std::string created_at;
  std::string updated_at;

  nlohmann::json extra = nlohmann::json::object();
};

PatchMetadata metadata_from_json(const nlohmann::json &j);
/// The fields that are set, over `extra`.
nlohmann::json metadata_to_json(const PatchMetadata &metadata);
/// Whether `j` holds nothing but megatoy's hash and timestamps.
bool holds_nothing_of_note(const nlohmann::json &j);
std::string iso8601_utc_now();

/**
 * Per-patch sidecars: the metadata of `<dir>/<name>.<ext>` is the JSON
 * object in `<dir>/.ym2612/<name>.<ext>.json`. An instrument inside a bank
 * or package file has its own under `<dir>/.ym2612/<name>.<ext>/`.
 */
namespace sidecar {

inline constexpr const char *kDirectory = ".ym2612";

/// The sidecar of `file`, or of the instrument `item` inside it.
std::filesystem::path path_for(const std::filesystem::path &file,
                               const std::string &item = {});

/**
 * The sidecar of `path`, which is a file or an instrument below one: the
 * nearest regular file at or above `path`, but not above `root`, is taken as
 * the file. Empty when there is none.
 */
std::filesystem::path path_for_patch(const std::filesystem::path &path,
                                     const std::filesystem::path &root);

/// The entry in `sidecar`: an empty object when there is no file, nullopt
/// when the file cannot be read as one.
std::optional<nlohmann::json> read(const std::filesystem::path &sidecar);

/**
 * Write `entry` as sorted, two-space indented JSON with a final newline. One
 * holding nothing of note removes the file instead, and the sidecar folder
 * if that leaves it empty.
 */
bool write(const std::filesystem::path &sidecar, const nlohmann::json &entry);

/// Move the sidecars of the file `from` to those of the file `to`.
bool move(const std::filesystem::path &from, const std::filesystem::path &to);

/// Remove the sidecars of the file `file`.
void remove(const std::filesystem::path &file);

/**
 * Split the single-file metadata at `<folder>/.ym2612/patches.json` and
 * `<folder>/.megatoy/patches.json` into per-patch sidecars, then remove
 * them. A field a sidecar already has is kept, `category` is dropped, and an
 * entry whose patch file is gone is dropped. A file that cannot be read is
 * left as it is.
 */
void migrate_folder(const std::filesystem::path &folder);

} // namespace sidecar

/**
 * The user's own metadata for read-only patches, such as the built-in
 * presets, kept in one file keyed by path. Each entry holds only what
 * differs from the patch's own sidecar: a field equal to it is not stored,
 * and one cleared where the sidecar sets it is stored with its empty value.
 */
class MetadataOverlay {
public:
  explicit MetadataOverlay(std::filesystem::path file);

  /// `base` with the stored differences for `key` laid over it.
  nlohmann::json apply(const std::string &key, nlohmann::json base) const;
  /// Store how `entry` differs from `base`.
  bool put(const std::string &key, const nlohmann::json &entry,
           const nlohmann::json &base);

private:
  std::filesystem::path file_;
  nlohmann::json root_ = nlohmann::json::object();
};

} // namespace patches
