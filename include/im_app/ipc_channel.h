#pragma once

#include <functional>
#include <memory>
#include <string>

namespace ImApp {

/// Cross-platform IPC channel between instances sharing the same key.
/// The primary instance binds + listens; secondary instances send and exit.
class IpcChannel {
  public:
    virtual ~IpcChannel() = default;

    /// Bind the server to a key so it can receive messages.
    /// Must be called before start_listening() on the primary instance.
    /// Returns true on success.
    virtual bool bind(const std::string& key) = 0;

    /// Start listening for incoming messages (called by the primary instance).
    virtual void start_listening() = 0;

    /// Send a message to the primary instance identified by `key`.
    /// Called by secondary instances before they exit.
    /// Returns true if the message was delivered, false if no listener exists.
    virtual bool send_message(const std::string& key,
                              const std::string& message) = 0;

    /// Callback invoked on the primary instance when a message arrives.
    using MessageHandler = std::function<void(const std::string& message)>;
    virtual void set_message_handler(MessageHandler handler) = 0;

    /// Factory — platform-specific implementation is selected at link time.
    static std::unique_ptr<IpcChannel> create();
};

} // namespace ImApp
