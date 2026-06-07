#include "darwin_instance_lock.h"

#include <cstdlib>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <sys/file.h>
#include <unistd.h>

namespace ImApp {

DarwinInstanceLock::DarwinInstanceLock(std::string key)
    : m_key(std::move(key)) {
    // Use $TMPDIR if available, otherwise /tmp.
    const char* tmp_dir = std::getenv("TMPDIR");
    std::filesystem::path lock_dir =
        tmp_dir != nullptr ? std::filesystem::path(tmp_dir) / "imapp"
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
}

DarwinInstanceLock::~DarwinInstanceLock() {
    if (m_lock_fd >= 0) {
        ::flock(m_lock_fd, LOCK_UN);
        ::close(m_lock_fd);
        ::unlink(m_lock_path.c_str());
    }
}

std::unique_ptr<InstanceLock> InstanceLock::create(std::string key) {
    return std::make_unique<DarwinInstanceLock>(std::move(key));
}

} // namespace ImApp
