// imnv — CLI launcher for ImNeovim.
// Usage: imnv [folder]
//   - With folder: open imnvim in that folder, exit immediately.
//   - Without folder: open imnvim in CWD, exit immediately.
//   - If an imnvim instance already exists for the folder, activate it instead.

#include <im_app/ipc_channel.h>

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <unistd.h>
#else
#include <climits>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#endif

#ifndef _WIN32
extern char** environ;
#endif

namespace {

std::string derive_key(const std::filesystem::path& path) {
    if (path.empty()) {
        return "__no_folder__";
    }
    auto abs_path = std::filesystem::absolute(path);
    std::string input = "ImNeovim:" + abs_path.string();
    auto hash = std::hash<std::string>{}(input);
    return std::to_string(hash);
}

/// Get the directory containing the current executable.
std::filesystem::path get_exe_dir() {
#ifdef _WIN32
    wchar_t exe_path[MAX_PATH];
    DWORD len = ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(std::wstring(exe_path, len)).parent_path();
#elif defined(__APPLE__)
    char exe_path[PATH_MAX];
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        return {};
    }
    return std::filesystem::path(exe_path).parent_path();
#else
    char exe_path[PATH_MAX];
    ssize_t len = ::readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) {
        return {};
    }
    exe_path[len] = '\0';
    return std::filesystem::path(exe_path).parent_path();
#endif
}

/// Get the full path to the imnvim executable next to imnv.
std::string get_imnvim_path() {
    auto dir = get_exe_dir();
#ifdef _WIN32
    return (dir / "imnvim.exe").string();
#else
    return (dir / "imnvim").string();
#endif
}

/// Spawn imnvim as a detached child process with the given working directory.
/// Returns true on success.
bool spawn_imnvim(const std::string& imnvim_path, const std::string& cwd) {
#ifdef _WIN32
    std::wstring wpath(imnvim_path.begin(), imnvim_path.end());
    std::wstring wcwd(cwd.begin(), cwd.end());
    // Pass the full executable path as argv[0] via the command line.
    std::wstring wargs = L"\"" + wpath + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    BOOL ok = ::CreateProcessW(nullptr, wargs.data(), nullptr, nullptr, FALSE,
                               CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
                               nullptr, wcwd.c_str(), &si, &pi);

    if (ok) {
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
        return true;
    }
    return false;
#else
    const char* argv[] = {imnvim_path.c_str(), nullptr};

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                     O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                     O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                     O_WRONLY, 0);

    pid_t pid;
    int rc = posix_spawn(&pid, imnvim_path.c_str(), &actions, &attr,
                         const_cast<char* const*>(argv), environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    return rc == 0;
#endif
}

} // anonymous namespace

int main(int argc, char** argv) {
    // Determine the target folder.
    std::filesystem::path folder;
    if (argc >= 2) {
        folder = argv[1];
    } else {
        folder = std::filesystem::current_path();
    }

    // Normalize to absolute path.
    std::error_code ec;
    folder = std::filesystem::absolute(folder, ec);
    if (ec) {
        folder = std::filesystem::current_path();
    }

    std::string key = derive_key(folder);

    // Try to activate an existing instance.
    auto ipc = ImApp::IpcChannel::create();
    if (ipc->send_message(key, "")) {
        // Existing instance was activated.
        return 0;
    }

    // No existing instance — spawn imnvim as a detached child process.
    std::string imnvim_path = get_imnvim_path();
    std::string folder_str = folder.string();

    if (!spawn_imnvim(imnvim_path, folder_str)) {
        // Fallback: try just the filename in case it's on PATH.
        spawn_imnvim("imnvim", folder_str);
    }

    return 0;
}
