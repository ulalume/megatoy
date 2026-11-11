#include "gui/library_undo.hpp"

#include "core/status.hpp"
#include "patches/patch_session.hpp"

#include <chrono>
#include <optional>
#include <system_error>

namespace ui::library_undo {

namespace {

constexpr std::chrono::seconds kOffered{8};

/// What a path held when the offer was made.
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
  taken.exists = std::filesystem::exists(path, ec) && !ec;
  if (!taken.exists) {
    return taken;
  }
  if (std::filesystem::is_regular_file(path, ec)) {
    taken.size = std::filesystem::file_size(path, ec);
  }
  taken.modified = std::filesystem::last_write_time(path, ec);
  return taken;
}

struct Offer {
  std::uint64_t toast = 0;
  std::vector<Snapshot> touched;
};

std::optional<Offer> g_offer;

bool within(const std::filesystem::path &path,
            const std::filesystem::path &touched) {
  const auto relative =
      path.lexically_normal().lexically_relative(touched.lexically_normal());
  return !relative.empty() && *relative.begin() != "..";
}

void withdraw() {
  if (g_offer) {
    megatoy::status::dismiss(g_offer->toast);
    g_offer.reset();
  }
}

} // namespace

void offer(std::string message, std::vector<std::filesystem::path> touched,
           std::function<void()> undo) {
  withdraw();
  Offer made;
  for (const auto &path : touched) {
    made.touched.push_back(snapshot(path));
  }
  made.toast = megatoy::status::post(megatoy::status::Severity::Success,
                                     std::move(message),
                                     {"Undo",
                                      [undo = std::move(undo)]() {
                                        g_offer.reset();
                                        undo();
                                      },
                                      kOffered});
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
  for (const auto &taken : g_offer->touched) {
    if (snapshot(taken.path) != taken) {
      withdraw();
      return;
    }
  }
  for (const auto &document : session.documents()) {
    if (!document->is_modified() || document->path().empty()) {
      continue;
    }
    for (const auto &taken : g_offer->touched) {
      if (within(document->path(), taken.path)) {
        withdraw();
        return;
      }
    }
  }
}

} // namespace ui::library_undo
