#pragma once

#include <filesystem>

namespace megatoy::workspace {

/// Resolve an existing path as far as possible, falling back to lexical form.
std::filesystem::path normalize_path(const std::filesystem::path &path);

/// True when both paths identify the same normalized filesystem location.
bool paths_equal(const std::filesystem::path &lhs,
                 const std::filesystem::path &rhs);

} // namespace megatoy::workspace
