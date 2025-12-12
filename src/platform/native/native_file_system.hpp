#pragma once

#include "platform/std_file_system.hpp"

// Desktop build now reuses the shared std::filesystem-backed implementation.
class NativeFileSystem : public platform::StdFileSystem {};
