#include "darwin_ipc_channel.h"

#include <cstring>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ImApp {

DarwinIpcChannel::DarwinIpcChannel() = default;

DarwinIpcChannel::~DarwinIpcChannel() { _close_all(); }

bool DarwinIpcChannel::bind(const std::string& key) {
    std::string addr_str = std::string("\0imapp_", 7) + key;

    m_server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_server_fd < 0) {
        spdlog::error("[DarwinIpcChannel] Failed to create socket");
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, addr_str.c_str(), addr_str.size());

    socklen_t addr_len = static_cast<socklen_t>(
        offsetof(struct sockaddr_un, sun_path) + addr_str.size());

    if (::bind(m_server_fd, reinterpret_cast<struct sockaddr*>(&addr),
               addr_len) < 0) {
        spdlog::error("[DarwinIpcChannel] Failed to bind socket");
        ::close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    if (::listen(m_server_fd, 4) < 0) {
        spdlog::error("[DarwinIpcChannel] Failed to listen on socket");
        ::close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    return true;
}

void DarwinIpcChannel::start_listening() {
    if (m_server_fd < 0) {
        return;
    }

    m_running = true;
    m_listen_thread = std::thread(&DarwinIpcChannel::_listen_loop, this);
}

bool DarwinIpcChannel::send_message(const std::string& key,
                                    const std::string& message) {
    std::string addr_str = std::string("\0imapp_", 7) + key;

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, addr_str.c_str(), addr_str.size());

    socklen_t addr_len = static_cast<socklen_t>(
        offsetof(struct sockaddr_un, sun_path) + addr_str.size());

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), addr_len) <
        0) {
        ::close(fd);
        return false;
    }

    ::send(fd, message.c_str(), message.size(), 0);
    ::close(fd);
    return true;
}

void DarwinIpcChannel::set_message_handler(MessageHandler handler) {
    m_handler = std::move(handler);
}

void DarwinIpcChannel::_close_all() {
    m_running = false;
    if (m_server_fd >= 0) {
        ::shutdown(m_server_fd, SHUT_RDWR);
        ::close(m_server_fd);
        m_server_fd = -1;
    }
    if (m_listen_thread.joinable()) {
        m_listen_thread.join();
    }
}

void DarwinIpcChannel::_listen_loop() {
    while (m_running) {
        int client_fd = ::accept(m_server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (!m_running) {
                break;
            }
            continue;
        }

        char buffer[4096];
        ssize_t n = ::read(client_fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            if (m_handler) {
                m_handler(std::string(buffer, static_cast<size_t>(n)));
            }
        }

        ::close(client_fd);
    }
}

std::unique_ptr<IpcChannel> IpcChannel::create() {
    return std::make_unique<DarwinIpcChannel>();
}

} // namespace ImApp
