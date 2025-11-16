#include "im_app/file_system.h"
#include <algorithm>
#include <mach-o/dyld.h>
#include <pwd.h>
#include <unistd.h>
#include <vector>

namespace ImApp {
std::filesystem::path FileSystem::executable_path() {
    std::vector<char> buf(1024, 0);
    uint32_t size = buf.size();
    int success = _NSGetExecutablePath(&buf[0], &size);
    if (success == -1) {
        // The buffer is not large enough.
        // `size` has been changed.
        buf.resize(size);
        std::fill(std::begin(buf), std::end(buf), 0);
        success = _NSGetExecutablePath(&buf[0], &size);
    }
    if (success == -1) {
        // The buffer is still not large enough.
        return std::filesystem::path();
    }
    auto ret = std::filesystem::path(buf.data());
    return ret;
}

std::filesystem::path FileSystem::local_app_data_path() {
    const auto *pwuid = getpwuid(getuid());
    std::filesystem::path pw_dir{pwuid->pw_dir};
    auto ret = pw_dir / "Library" / "Caches";
    return ret;
}
} // namespace ImApp
