#pragma once

#include "im_app/instance_lock.h"

#include <string>

namespace ImApp {

class LinuxInstanceLock : public InstanceLock {
  public:
    explicit LinuxInstanceLock(std::string key);
    ~LinuxInstanceLock() override;

    bool acquired() const override { return m_acquired; }
    const std::string& key() const override { return m_key; }

  private:
    std::string m_key;
    bool m_acquired{false};
    int m_lock_fd{-1};
    std::string m_lock_path;
};

} // namespace ImApp
