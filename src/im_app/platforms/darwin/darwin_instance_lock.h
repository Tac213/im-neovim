#pragma once

#include "im_app/instance_lock.h"

#include <string>

namespace ImApp {

class DarwinInstanceLock : public InstanceLock {
  public:
    explicit DarwinInstanceLock(std::string key);
    ~DarwinInstanceLock() override;

    bool acquired() const override { return m_acquired; }
    const std::string& key() const override { return m_key; }

  private:
    std::string m_key;
    bool m_acquired{false};
    int m_lock_fd{-1};
    std::string m_lock_path;
};

} // namespace ImApp
