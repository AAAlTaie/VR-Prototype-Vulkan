#include "platform/Paths.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace platform {
namespace {

std::filesystem::path executablePath() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size()) {
            buffer.resize(written);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()));
#else
    std::error_code error;
    const std::filesystem::path link = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : link;
#endif
}

}

std::filesystem::path executableDirectory() {
    const std::filesystem::path path = executablePath();
    return path.empty() ? std::filesystem::current_path() : path.parent_path();
}

std::vector<std::filesystem::path> resourceSearchPaths(const std::filesystem::path& relative) {
    std::vector<std::filesystem::path> candidates{std::filesystem::current_path() / relative,
                                                  executableDirectory() / relative};

    std::error_code error;
    if (std::filesystem::equivalent(candidates.front().parent_path(),
                                    candidates.back().parent_path(), error)) {
        candidates.pop_back();
    }
    return candidates;
}

core::Result<std::filesystem::path> resolveResource(const std::filesystem::path& requested) {
    std::error_code error;
    if (requested.is_absolute()) {
        if (std::filesystem::exists(requested, error)) {
            return requested;
        }
        return core::Error{"file not found: " + requested.string()};
    }

    std::string attempted;
    for (const std::filesystem::path& candidate : resourceSearchPaths(requested)) {
        if (std::filesystem::exists(candidate, error)) {
            return candidate;
        }
        attempted.append(attempted.empty() ? "" : ", ").append(candidate.string());
    }
    return core::Error{"file not found, searched: " + attempted};
}

}
