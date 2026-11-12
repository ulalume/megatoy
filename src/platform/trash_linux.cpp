#include "platform/trash.hpp"

// The freedesktop.org Trash specification: a file goes to the home Trash when
// it is on the same file system, and otherwise to the Trash at the top of its
// own; each holds the file under files/ and where it came from under info/.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace platform::trash {
namespace {

namespace fs = std::filesystem;

bool make_private_dir(const fs::path &dir) {
  return ::mkdir(dir.c_str(), 0700) == 0 || errno == EEXIST;
}

/// A directory, not a link, that only its owner, the current user, can use.
bool is_own_dir(const fs::path &dir) {
  struct stat st{};
  return ::lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
         st.st_uid == ::getuid();
}

/// The home Trash, created if it is not there yet.
std::optional<fs::path> home_trash() {
  fs::path data;
  if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg == '/') {
    data = xdg;
  } else if (const char *home = std::getenv("HOME"); home && *home == '/') {
    data = fs::path(home) / ".local" / "share";
  } else {
    return std::nullopt;
  }
  std::error_code ec;
  fs::create_directories(data, ec);
  const fs::path trash = data / "Trash";
  if (!make_private_dir(trash) || !is_own_dir(trash)) {
    return std::nullopt;
  }
  return trash;
}

/// The top of the file system `path` is on: the last directory upwards still
/// on the same device.
fs::path top_of(const fs::path &path, dev_t device) {
  fs::path top = path.parent_path();
  while (top.has_relative_path()) {
    const fs::path up = top.parent_path();
    struct stat st{};
    if (::stat(up.c_str(), &st) != 0 || st.st_dev != device) {
      break;
    }
    top = up;
  }
  return top;
}

/// The Trash at `top`: $top/.Trash/$uid when an administrator has set up
/// $top/.Trash (a sticky directory, not a link), else $top/.Trash-$uid.
std::optional<fs::path> top_trash(const fs::path &top) {
  const std::string uid = std::to_string(::getuid());
  const fs::path shared = top / ".Trash";
  struct stat st{};
  if (::lstat(shared.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
      (st.st_mode & S_ISVTX) != 0) {
    const fs::path mine = shared / uid;
    if (make_private_dir(mine) && is_own_dir(mine)) {
      return mine;
    }
  }
  const fs::path own = top / (".Trash-" + uid);
  if (make_private_dir(own) && is_own_dir(own)) {
    return own;
  }
  return std::nullopt;
}

/// Percent-encoded as a URL path, which is how the info file writes it.
std::string url_path(const std::string &path) {
  static const char *hex = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char c : path) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

std::string deletion_date() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  ::localtime_r(&now, &local);
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &local);
  return text;
}

/// The info file of what the Trash keeps at `in_trash` (`trash`/files/name).
fs::path info_of(const fs::path &in_trash) {
  return in_trash.parent_path().parent_path() / "info" /
         (in_trash.filename().string() + ".trashinfo");
}

} // namespace

bool available() { return true; }

const char *name() { return "Trash"; }

std::optional<fs::path> move_to_trash(const fs::path &given) {
  std::error_code ec;
  const fs::path path = fs::absolute(given, ec);
  struct stat st{};
  if (::lstat(path.c_str(), &st) != 0) {
    return std::nullopt;
  }

  std::optional<fs::path> trash = home_trash();
  fs::path recorded = path; // absolute in the home Trash
  struct stat trash_st{};
  if (!trash || ::stat(trash->c_str(), &trash_st) != 0 ||
      trash_st.st_dev != st.st_dev) {
    const fs::path top = top_of(path, st.st_dev);
    trash = top_trash(top);
    // relative to the top in a Trash of its own
    recorded = path.lexically_relative(top);
  }
  if (!trash) {
    return std::nullopt;
  }
  const fs::path files = *trash / "files";
  const fs::path info = *trash / "info";
  if (!make_private_dir(files) || !make_private_dir(info)) {
    return std::nullopt;
  }

  const std::string contents =
      "[Trash Info]\nPath=" + url_path(recorded.string()) +
      "\nDeletionDate=" + deletion_date() + "\n";

  // The info file is created first, exclusively: that is what claims a name.
  const fs::path base = path.filename();
  for (int n = 1; n < 10000; ++n) {
    const std::string name = n == 1 ? base.string()
                                    : base.stem().string() + "." +
                                          std::to_string(n) +
                                          base.extension().string();
    const fs::path target = files / name;
    const fs::path info_file = info / (name + ".trashinfo");
    struct stat taken{};
    if (::lstat(target.c_str(), &taken) == 0) {
      continue;
    }
    const int fd = ::open(info_file.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
      if (errno == EEXIST) {
        continue;
      }
      return std::nullopt;
    }
    const bool written = ::write(fd, contents.data(), contents.size()) ==
                         static_cast<ssize_t>(contents.size());
    ::close(fd);
    if (!written || ::rename(path.c_str(), target.c_str()) != 0) {
      ::unlink(info_file.c_str());
      return std::nullopt;
    }
    return target;
  }
  return std::nullopt;
}

bool put_back(const fs::path &in_trash, const fs::path &path) {
  if (::rename(in_trash.c_str(), path.c_str()) != 0) {
    return false;
  }
  ::unlink(info_of(in_trash).c_str());
  return true;
}

} // namespace platform::trash
