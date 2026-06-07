#include "linux_ipc_channel.h"

#include <cstring>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ImApp {

LinuxIpcChannel::LinuxIpcChannel() = default;

LinuxIpcChannel::~LinuxIpcChannel() { _close_all(); }

bool LinuxIpcChannel::bind(const std::string& key) {
    std::string addr_str = std::string("\0imapp_", 7) + key;

    m_server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_server_fd < 0) {
        spdlog::error("[LinuxIpcChannel] Failed to create socket");
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, addr_str.c_str(), addr_str.size());

    socklen_t addr_len = static_cast<socklen_t>(
        offsetof(struct sockaddr_un, sun_path) + addr_str.size());

    if (::bind(m_server_fd, reinterpret_cast<struct sockaddr*>(&addr),
               addr_len) < 0) {
        spdlog::error("[LinuxIpcChannel] Failed to bind socket");
        ::close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    if (::listen(m_server_fd, 4) < 0) {
        spdlog::error("[LinuxIpcChannel] Failed to listen on socket");
        ::close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    return true;
}

void LinuxIpcChannel::start_listening() {
    if (m_server_fd < 0) {
        return;
    }

    m_running = true;
    m_listen_thread = std::thread(&LinuxIpcChannel::_listen_loop, this);
}

bool LinuxIpcChannel::send_message(const std::string& key,
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
        return false; // No listener
    }

    // Send the message.
    ::send(fd, message.c_str(), message.size(), 0);
    ::close(fd);
    return true;
}

void LinuxIpcChannel::set_message_handler(MessageHandler handler) {
    m_handler = std::move(handler);
}

void LinuxIpcChannel::_close_all() {
    m_running = false;
    if (m_server_fd >= 0) {
        ::close(m_server_fd);
        m_server_fd = -1;
    }
    if (m_listen_thread.joinable()) {
        m_listen_thread.join();
    }
}

void LinuxIpcChannel::_listen_loop() {
    while (m_running) {
        struct pollfd pfd;
        pfd.fd = m_server_fd;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, 100);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ret == 0) {
            continue;
        }

        int client_fd = ::accept(m_server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (!m_running) {
                break;
            }
            continue;
        }

        char buffer[4096];
        ssize_t n = ::read(client_fd, buffer, sizeof(buffer) - 1);
        if (n >= 0) {
            buffer[n] = '\0';
            if (m_handler) {
                m_handler(std::string(buffer, static_cast<size_t>(n)));
            }
        }

        ::close(client_fd);
    }
}

std::unique_ptr<IpcChannel> IpcChannel::create() {
    return std::make_unique<LinuxIpcChannel>();
}

} // namespace ImApp
