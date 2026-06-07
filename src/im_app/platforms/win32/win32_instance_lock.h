#pragma once

#include "im_app/instance_lock.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <string>

namespace ImApp {

class Win32InstanceLock : public InstanceLock {
  public:
    explicit Win32InstanceLock(std::string key);
    ~Win32InstanceLock() override;

    bool acquired() const override { return m_acquired; }
    const std::string& key() const override { return m_key; }

  private:
    std::string m_key;
    bool m_acquired{false};
    HANDLE m_mutex{nullptr};
};

} // namespace ImApp
