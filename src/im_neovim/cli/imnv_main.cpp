// imnv — CLI launcher for ImNeovim.
// Usage: imnv [path1[;path2...]]   (; on Windows, : on Unix)
//   - With paths: open imnvim with those folders, exit immediately.
//   - Without paths: open imnvim with empty workspace, exit immediately.
//   - If an imnvim instance already exists for the same set of folders,
//     activate it instead.

#include <im_app/ipc_channel.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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

/// Derive an instance key from all folder paths combined.
std::string derive_key(const std::vector<std::string>& folders) {
    if (folders.empty()) {
        return {};
    }
    std::string input = "ImNeovim:";
    for (const auto& f : folders) {
        auto abs_path = std::filesystem::absolute(f);
        input += abs_path.string();
    }
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

/// Spawn imnvim as a detached child process, passing the given argument
/// string (if non-empty) as argv[1].
/// Returns true on success.
bool spawn_imnvim(const std::string& imnvim_path, const std::string& arg) {
#ifdef _WIN32
    std::wstring wpath(imnvim_path.begin(), imnvim_path.end());

    std::wstring wargs = L"\"" + wpath + L"\"";
    if (!arg.empty()) {
        std::wstring warg(arg.begin(), arg.end());
        wargs += L" \"" + warg + L"\"";
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    BOOL ok = ::CreateProcessW(nullptr, wargs.data(), nullptr, nullptr, FALSE,
                               CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
                               nullptr, nullptr, &si, &pi);

    if (ok) {
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
        return true;
    }
    return false;
#else
    std::vector<const char*> argv;
    argv.push_back(imnvim_path.c_str());
    if (!arg.empty()) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

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
                         const_cast<char* const*>(argv.data()), environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    return rc == 0;
#endif
}

} // anonymous namespace

int main(int argc, char** argv) {
    // Parse argv[1] for workspace folders, split on the platform
    // path separator (matching imnvim's own argv parsing).
#ifdef _WIN32
    constexpr char path_sep = ';';
#else
    constexpr char path_sep = ':';
#endif

    std::vector<std::string> folders;
    if (argc >= 2) {
        std::string_view arg{argv[1]};
        size_t start = 0;
        while (start < arg.size()) {
            auto end = arg.find(path_sep, start);
            if (end == std::string_view::npos) {
                end = arg.size();
            }
            if (end > start) {
                auto token = arg.substr(start, end - start);
                std::error_code ec;
                std::filesystem::path p{token};
                p = std::filesystem::absolute(p, ec);
                if (!ec && std::filesystem::is_directory(p)) {
                    folders.push_back(p.string());
                }
            }
            start = end + 1;
        }
    }

    std::string key = derive_key(folders);

    // If we have folders, try to activate an existing instance.
    if (!key.empty()) {
        auto ipc = ImApp::IpcChannel::create();
        if (ipc->send_message(key, "")) {
            return 0;
        }
    }

    // No existing instance — spawn imnvim as a detached child process.
    std::string imnvim_path = get_imnvim_path();
    std::string arg = (argc >= 2) ? argv[1] : "";

    if (!spawn_imnvim(imnvim_path, arg)) {
        spawn_imnvim("imnvim", arg);
    }

    return 0;
}
