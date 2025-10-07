#include "im_app/file_system.h"
#include <pwd.h>
#include <unistd.h>
#include <vector>

namespace ImApp {
std::filesystem::path FileSystem::executable_path() {
    std::vector<char> buf(1024, 0);
    auto size = buf.size();
    bool have_path = false;
    bool should_continue = true;
    do {
        ssize_t result = readlink("/proc/self/exe", &buf[0], size);
        if (result < 0) {
            should_continue = false;
        } else if (static_cast<size_t>(result) < size) {
            have_path = true;
            should_continue = false;
            size = result;
        } else {
            size *= 2;
            buf.resize(size);
            std::fill(std::begin(buf), std::end(buf), 0);
        }
    } while (should_continue);
    if (!have_path) {
        return std::filesystem::path();
    }
    auto ret = std::filesystem::path(buf.data());
    return ret;
}

std::filesystem::path FileSystem::local_app_data_path() {
    const auto* pwuid = getpwuid(getuid());
    std::filesystem::path pw_dir{pwuid->pw_dir};
    auto ret = pw_dir / ".cache";
    return ret;
}
} // namespace ImApp
