#pragma once

#include <memory>
#include <string>

namespace ImApp {

/// Cross-platform named lock for single-instance detection.
/// The lock is keyed by an arbitrary string (e.g., a hash of a folder path).
/// On construction, attempts to acquire the named lock.
/// On destruction, releases it.
class InstanceLock {
  public:
    virtual ~InstanceLock() = default;

    /// True if THIS process acquired the lock (i.e., we are the first/primary
    /// instance for the given key).
    virtual bool acquired() const = 0;

    /// The key this lock was created for.
    virtual const std::string& key() const = 0;

    /// Factory — platform-specific implementation is selected at link time.
    static std::unique_ptr<InstanceLock> create(std::string key);
};

} // namespace ImApp
