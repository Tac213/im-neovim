#include "linux_instance_lock.h"

#include <cstdlib>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <sys/file.h>
#include <unistd.h>

namespace ImApp {

LinuxInstanceLock::LinuxInstanceLock(std::string key) : m_key(std::move(key)) {
    // Use $XDG_RUNTIME_DIR if available, otherwise /tmp.
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    std::filesystem::path lock_dir =
        runtime_dir != nullptr ? std::filesystem::path(runtime_dir) / "imapp"
                               : std::filesystem::path("/tmp") / "imapp";

    std::error_code ec;
    std::filesystem::create_directories(lock_dir, ec);

    m_lock_path = (lock_dir / ("instance_" + m_key + ".lock")).string();

    m_lock_fd = ::open(m_lock_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (m_lock_fd < 0) {
        spdlog::error("[InstanceLock] Failed to open lock file: {}",
                      m_lock_path);
        return;
    }

    if (::flock(m_lock_fd, LOCK_EX | LOCK_NB) == 0) {
        m_acquired = true;
    }
    // If flock fails, another instance holds the lock.
}

LinuxInstanceLock::~LinuxInstanceLock() {
    if (m_lock_fd >= 0) {
        ::flock(m_lock_fd, LOCK_UN);
        ::close(m_lock_fd);
        ::unlink(m_lock_path.c_str());
    }
}

std::unique_ptr<InstanceLock> InstanceLock::create(std::string key) {
    return std::make_unique<LinuxInstanceLock>(std::move(key));
}

} // namespace ImApp
