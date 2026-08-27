#pragma once

#include <filesystem>
#include <vector>

#include "core/Result.h"

namespace platform {

std::filesystem::path executableDirectory();

std::vector<std::filesystem::path> resourceSearchPaths(const std::filesystem::path& relative);

core::Result<std::filesystem::path> resolveResource(const std::filesystem::path& requested);

}
