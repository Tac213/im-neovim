#include "darwin_ipc_channel.h"

#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace ImApp {

DarwinIpcChannel::DarwinIpcChannel() = default;

DarwinIpcChannel::~DarwinIpcChannel() { _close_all(); }

bool DarwinIpcChannel::bind(const std::string& key) {
    // Use filesystem-bound Unix socket (macOS does not support Linux-style
    // abstract namespace sockets with '\0' prefix).
    const char* tmp_dir = std::getenv("TMPDIR");
    std::string dir =
        tmp_dir != nullptr ? std::string(tmp_dir) + "imapp" : "/tmp/imapp";

    // Ensure the directory exists (best-effort; InstanceLock also creates it).
    ::mkdir(dir.c_str(), 0755);

    m_socket_path = dir + "/ipc_" + key;

    m_server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_server_fd < 0) {
        spdlog::error("[DarwinIpcChannel] Failed to create socket");
        return false;
    }

    // Remove any stale socket file from a previous run.
    ::unlink(m_socket_path.c_str());

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, m_socket_path.c_str(),
                 sizeof(addr.sun_path) - 1);

    if (::bind(m_server_fd, reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        spdlog::error("[DarwinIpcChannel] Failed to bind socket: {}",
                      m_socket_path);
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
    // Use the same filesystem path as bind().
    const char* tmp_dir = std::getenv("TMPDIR");
    std::string dir =
        tmp_dir != nullptr ? std::string(tmp_dir) + "imapp" : "/tmp/imapp";
    std::string socket_path = dir + "/ipc_" + key;

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) <
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
        ::close(m_server_fd);
        m_server_fd = -1;
    }
    if (m_listen_thread.joinable()) {
        m_listen_thread.join();
    }
    if (!m_socket_path.empty()) {
        ::unlink(m_socket_path.c_str());
    }
}

void DarwinIpcChannel::_listen_loop() {
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
    return std::make_unique<DarwinIpcChannel>();
}

} // namespace ImApp
