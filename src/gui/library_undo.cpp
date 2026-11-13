#include "gui/library_undo.hpp"

#include "core/status.hpp"
#include "patches/patch_session.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <system_error>
#include <utility>

namespace ui::library_undo {

namespace {

/// How often what the offer would undo is looked at on disk.
constexpr std::chrono::milliseconds kCheckEvery{250};

/// What a path held when the offer was made, from one look at the disk.
struct Snapshot {
  std::filesystem::path path;
  bool exists = false;
  std::uintmax_t size = 0;
  std::filesystem::file_time_type modified{};

  bool operator==(const Snapshot &) const = default;
};

Snapshot snapshot(const std::filesystem::path &path) {
  Snapshot taken{path};
  std::error_code ec;
  const std::filesystem::directory_entry entry(path, ec);
  taken.exists = !ec && entry.exists(ec);
  if (taken.exists) {
    taken.size = entry.is_regular_file(ec) ? entry.file_size(ec) : 0;
    taken.modified = entry.last_write_time(ec);
  }
  return taken;
}

bool within(const std::filesystem::path &path,
            const std::filesystem::path &touched) {
  const auto relative =
      path.lexically_normal().lexically_relative(touched.lexically_normal());
  return !relative.empty() && *relative.begin() != "..";
}

struct Offer {
  std::uint64_t toast = 0;
  std::vector<Snapshot> touched;
  /// The open patches at `touched` as they stood when first seen: an edit
  /// made since withdraws the offer, one made before does not.
  std::optional<std::vector<std::pair<patches::DocumentId, ym2612::Patch>>>
      documents;
  std::chrono::steady_clock::time_point next_check{};
};

std::optional<Offer> g_offer;

void withdraw() {
  if (g_offer) {
    megatoy::status::dismiss(g_offer->toast);
    g_offer.reset();
  }
}

/// The open patches whose files are at `touched` or inside it.
std::vector<std::pair<patches::DocumentId, ym2612::Patch>>
documents_at(const patches::PatchSession &session,
             const std::vector<Snapshot> &touched) {
  std::vector<std::pair<patches::DocumentId, ym2612::Patch>> found;
  for (const auto &document : session.documents()) {
    if (document->path().empty()) {
      continue;
    }
    const auto path = session.repository().to_absolute_path(document->path());
    if (std::any_of(touched.begin(), touched.end(), [&](const auto &taken) {
          return within(path, taken.path);
        })) {
      found.emplace_back(document->id(), document->patch());
    }
  }
  return found;
}

} // namespace

void offer(std::string message, std::vector<std::filesystem::path> touched,
           std::function<void()> undo) {
  withdraw();
  Offer made;
  for (const auto &path : touched) {
    made.touched.push_back(snapshot(path));
  }
  made.toast = megatoy::status::post(
      megatoy::status::Severity::Success, std::move(message),
      megatoy::status::lasts::kMedium, {"Undo", [undo = std::move(undo)]() {
                                          g_offer.reset();
                                          undo();
                                        }});
  g_offer = std::move(made);
}

void check(const patches::PatchSession &session) {
  if (!g_offer) {
    return;
  }
  if (!megatoy::status::shown(g_offer->toast)) {
    g_offer.reset();
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now < g_offer->next_check) {
    return;
  }
  g_offer->next_check = now + kCheckEvery;

  for (const auto &taken : g_offer->touched) {
    if (snapshot(taken.path) != taken) {
      withdraw();
      return;
    }
  }
  const auto documents = documents_at(session, g_offer->touched);
  if (!g_offer->documents) {
    g_offer->documents = documents;
    return;
  }
  // One opened there since is taken as it opens.
  auto &before = *g_offer->documents;
  for (const auto &[id, patch] : documents) {
    const auto was =
        std::find_if(before.begin(), before.end(),
                     [id](const auto &seen) { return seen.first == id; });
    if (was == before.end()) {
      before.emplace_back(id, patch);
    } else if (!(was->second == patch)) {
      withdraw();
      return;
    }
  }
}

} // namespace ui::library_undo
