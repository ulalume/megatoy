#pragma once

#include "platform/std_file_system.hpp"

namespace platform::web {

// Web build shares the same std::filesystem-backed implementation.
class WebFileSystem : public platform::StdFileSystem {};

} // namespace platform::web
