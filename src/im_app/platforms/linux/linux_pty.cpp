#include "linux_pty.h"
#include <csignal>  // For SIGTERM
#include <errno.h>  // For errno
#include <fcntl.h>  // For O_RDWR, O_RDWR
#include <limits.h> // For PATH_MAX
#include <pwd.h>    // For getpwuid
#include <spdlog/spdlog.h>
#include <stdlib.h>    // For getenv, setenv, unsetenv, realpath
#include <string.h>    // For strrchr, strcpy, strncpy, strerror
#include <sys/ioctl.h> // For ioctl
#include <termios.h>   // For termios
#include <unistd.h>

namespace ImApp {
LinuxPseudoTerminal::~LinuxPseudoTerminal() {
    if (m_pty_fd >= 0) {
        close(m_pty_fd);
    }
    if (m_child_pid > 0) {
        kill(m_child_pid, SIGTERM);
    }
}

bool LinuxPseudoTerminal::launch(uint16_t row, uint16_t col) {
    if (is_valid()) {
        return true;
    }
    // Open PTY master
    m_pty_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (m_pty_fd < 0) {
        spdlog::critical("Failed to call posix_openpt!");
        return false;
    }
    if (grantpt(m_pty_fd) < 0) {
        spdlog::critical("Failed to call grantpt!");
        close(m_pty_fd);
        m_pty_fd = -1;
        return false;
    }
    if (unlockpt(m_pty_fd) < 0) {
        spdlog::critical("Failed to call unlockpt!");
        close(m_pty_fd);
        m_pty_fd = -1;
        return false;
    }
    char* slave_name = ptsname(m_pty_fd);
    if (!slave_name) {
        spdlog::critical("Failed to get the slave's name!");
        close(m_pty_fd);
        m_pty_fd = -1;
        return false;
    }

    m_child_pid = fork();

    if (m_child_pid < 0) {
        spdlog::critical("Failed to fork current process!");
        close(m_pty_fd);
        m_pty_fd = -1;
        return false;
    }

    if (m_child_pid == 0) {
        /* We are now in the forked child process. */
        close(m_pty_fd); // Close master PTY in child

        if (setsid() < 0) {
            // Create new session, detach from parent's controlling TTY
            spdlog::critical("Failed to call setsid!");
            exit(EXIT_FAILURE);
        }

        // Open slave PTY
        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) {
            spdlog::critical("Failed to slave PTY!");
            exit(EXIT_FAILURE);
        }

        // Make the slave PTY the controlling terminal for this new session
        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0) {
            // This can fail if the process is not a session leader and already
            // has a controlling TTY. setsid() should make us a session leader.
            spdlog::warn(
                "ioctl TIOCSCTTY failed (can be non-fatal depending on "
                "context)");
        }

        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);

        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        // Configure terminal modes for the slave PTY
        struct termios tios;
        if (tcgetattr(STDIN_FILENO, &tios) < 0) {
            spdlog::critical("tcgetattr failed on slave pty");
            exit(EXIT_FAILURE);
        }

        // Set reasonable default modes (from st/typical terminal settings)
        tios.c_iflag = ICRNL | IXON | IXANY | IMAXBEL | BRKINT;
#if defined(IUTF8) // Common on Linux, good to enable if available
        tios.c_iflag |= IUTF8;
#endif
        tios.c_oflag =
            OPOST |
            ONLCR; // OPOST: enable output processing, ONLCR: map NL to CR-NL

        tios.c_cflag &= ~(CSIZE | PARENB); // Clear size and parity bits
        tios.c_cflag |= CS8;               // 8 bits per character
        tios.c_cflag |= CREAD;             // Enable receiver
        tios.c_cflag |= HUPCL; // Hang up on last close (sends SIGHUP to
                               // foreground process group)

        // Standard local modes for interactive shells
        tios.c_lflag =
            ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &tios) < 0) {
            spdlog::critical("tcsetattr failed on slave pty");
            exit(EXIT_FAILURE);
        }

        // Set window size
        struct winsize ws = {};
        ws.ws_row = row;
        ws.ws_col = col;
        if (ioctl(STDIN_FILENO, TIOCSWINSZ, &ws) < 0) {
            spdlog::warn(
                "ioctl TIOCSWINSZ failed on slave pty (non-fatal, shell "
                "might misbehave)");
        }

        // Prepare environment for the shell
        setenv("TERM", "xterm-256color", 1);
        // Unsetting these is often good as the login shell will set them
        // appropriately
        unsetenv("COLUMNS");
        unsetenv("LINES");
        // Optionally, clear other inherited variables that might cause issues,
        // e.g., unsetenv("TERMCAP"); unsetenv("WINDOWID"); // Common in some
        // terminal emulators

        // Logic for Linux and other Unix-like systems (also launch as login
        // shell)
        char shell_path_buf[PATH_MAX];
        const char* shell_env_val =
            getenv("SHELL"); // SHELL env var is primary on Linux

        if (shell_env_val && shell_env_val[0] != '\0') {
            strncpy(shell_path_buf, shell_env_val, sizeof(shell_path_buf));
            shell_path_buf[sizeof(shell_path_buf) - 1] = '\0';
        } else {
            struct passwd* pw_linux =
                getpwuid(getuid()); // Fallback to passwd entry
            if (pw_linux && pw_linux->pw_shell &&
                pw_linux->pw_shell[0] != '\0') {
                strncpy(shell_path_buf, pw_linux->pw_shell,
                        sizeof(shell_path_buf));
                shell_path_buf[sizeof(shell_path_buf) - 1] = '\0';
            } else {
                strncpy(shell_path_buf, "/bin/bash",
                        sizeof(shell_path_buf)); // Absolute fallback for Linux
                shell_path_buf[sizeof(shell_path_buf) - 1] = '\0';
            }
        }

        char shell_argv0_login_linux[PATH_MAX + 1];
        shell_argv0_login_linux[0] = '-';
        const char* shell_basename_linux = strrchr(shell_path_buf, '/');
        if (shell_basename_linux) {
            strncpy(shell_argv0_login_linux + 1, shell_basename_linux + 1,
                    sizeof(shell_argv0_login_linux) - 2);
        } else {
            strncpy(shell_argv0_login_linux + 1, shell_path_buf,
                    sizeof(shell_argv0_login_linux) - 2);
        }
        shell_argv0_login_linux[sizeof(shell_argv0_login_linux) - 1] = '\0';

        char* const new_argv_linux[] = {shell_argv0_login_linux, nullptr};

#if defined(IM_APP_DEBUG)
        spdlog::debug("[PTY DEBUG] Linux/Other Shell Launch Information:");
        struct passwd* pw_linux_debug = getpwuid(getuid());
        spdlog::debug("  User's pw_shell (from getpwuid): '{}'",
                      (pw_linux_debug && pw_linux_debug->pw_shell)
                          ? pw_linux_debug->pw_shell
                          : "(not found or empty)");
        const char* env_shell_child_linux = getenv("SHELL");
        spdlog::debug("  getenv(\"SHELL\") in child process: '{}'",
                      env_shell_child_linux ? env_shell_child_linux
                                            : "(not set or empty)");
        spdlog::debug("  Path to be executed (shell_path_buf): '{}'",
                      shell_path_buf);
        spdlog::debug("  argv[0] for child shell (new_argv_linux[0]): '{}'",
                      new_argv_linux[0] ? new_argv_linux[0] : "(NULL)");
#endif

        execv(shell_path_buf, new_argv_linux);

        spdlog::critical(
            "FATAL: Failed to execv shell '{}' (intended argv[0]='{}'): {}",
            shell_path_buf, new_argv_linux[0] ? new_argv_linux[0] : "(null)",
            strerror(errno));
        exit(127); // Standard exit code for command
    }

    return true;
}

void LinuxPseudoTerminal::terminate() {

    if (m_pty_fd >= 0) {
        close(m_pty_fd);
    }
    m_pty_fd = -1;
    if (m_child_pid > 0) {
        kill(m_child_pid, SIGTERM);
    }
    m_child_pid = -1;
}

bool LinuxPseudoTerminal::is_valid() {
    return m_pty_fd >= 0 && m_child_pid > 0;
}

size_t LinuxPseudoTerminal::write(const void* buff, size_t size) {
    if (m_pty_fd < 0) {
        return 0;
    }
    return ::write(m_pty_fd, buff, size);
}

size_t LinuxPseudoTerminal::read(void* buff, size_t size) {
    if (m_pty_fd < 0) {
        return 0;
    }
    return ::read(m_pty_fd, buff, size);
}

bool LinuxPseudoTerminal::resize(uint16_t row, uint16_t col) {
    if (m_pty_fd < 0) {
        return false;
    }
    struct winsize ws = {};
    ws.ws_row = row;
    ws.ws_col = col;
    return ioctl(m_pty_fd, TIOCSWINSZ, &ws) >= 0;
}

std::shared_ptr<PseudoTerminal> PseudoTerminal::create() {
    return std::make_shared<LinuxPseudoTerminal>();
}
} // namespace ImApp
