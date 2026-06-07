#include "win32_instance_lock.h"

#include <string>

namespace ImApp {

Win32InstanceLock::Win32InstanceLock(std::string key) : m_key(std::move(key)) {
    // Build a mutex name from the key: Global\ImApp_Instance_{key}
    std::wstring wide_key(m_key.begin(), m_key.end());
    std::wstring mutex_name = L"Global\\ImApp_Instance_" + wide_key;

    m_mutex = ::CreateMutexW(nullptr, TRUE, mutex_name.c_str());
    if (m_mutex != nullptr && ::GetLastError() != ERROR_ALREADY_EXISTS) {
        m_acquired = true;
    }
}

Win32InstanceLock::~Win32InstanceLock() {
    if (m_mutex != nullptr) {
        ::CloseHandle(m_mutex);
    }
}

std::unique_ptr<InstanceLock> InstanceLock::create(std::string key) {
    return std::make_unique<Win32InstanceLock>(std::move(key));
}

} // namespace ImApp
