// imnv — CLI launcher for ImNeovim.
// Usage: imnv [options] [path...]
//   Options:
//     --help, -h     Show this help message and exit.
//     --version, -v  Show version information and exit.
//   Positional arguments:
//     <folder>       Open the folder in the workspace.
//     <file>         Open the file for editing.
//   If an imnvim instance already exists for the same set of folders,
//   it will be activated and any files will be opened there.

#include <im_app/ipc_channel.h>
#include <im_neovim/build_config.h>

#include <filesystem>
#include <iostream>
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

/// Classification result for a positional path argument.
enum class PathKind { Folder, File, Invalid };

/// Parsed command-line arguments.
struct ParsedArgs {
    std::vector<std::string> folders;
    std::vector<std::string> files;
};

/// Convert a filesystem path to a UTF-8 string.
std::string path_to_utf8(const std::filesystem::path& p) {
    auto u8p = p.u8string();
    return std::string{u8p.begin(), u8p.end()};
}

/// Derive an instance key from folder paths only (files do not define
/// instance identity).
std::string derive_key(const std::vector<std::string>& folders) {
    if (folders.empty()) {
        return {};
    }
    std::string input = "ImNeovim:";
    for (const auto& f : folders) {
        auto abs_path = std::filesystem::weakly_canonical(f);
        input += path_to_utf8(abs_path);
    }
    auto hash = std::hash<std::string>{}(input);
    return std::to_string(hash);
}

/// Classify a raw path argument as a folder, file, or invalid.
/// Folders must exist and be directories.
/// Files: the parent directory must exist, but the file itself does not
/// need to exist (nvim treats it as a new file).
PathKind classify_path(std::string_view raw) {
    std::error_code ec;
    std::filesystem::path p{raw};

    auto status = std::filesystem::status(p, ec);
    // Note: ec may be set to ENOENT on some platforms for
    // non-existent paths.  The subsequent checks handle all cases
    // correctly regardless.

    if (std::filesystem::is_directory(status)) {
        return PathKind::Folder;
    }

    if (std::filesystem::is_regular_file(status)) {
        return PathKind::File;
    }

    // Path does not exist — check if the parent directory exists.
    // If so, it is a valid new-file target for nvim.
    // An empty parent means a bare filename relative to CWD, which
    // always exists.
    if (!std::filesystem::exists(status)) {
        auto parent = p.parent_path();
        if (parent.empty() || std::filesystem::is_directory(parent, ec)) {
            return PathKind::File;
        }
    }

    return PathKind::Invalid;
}

/// Encode file paths into an IPC message string.
/// Format: "FILES\n<path>\n<path>..."
/// Returns an empty string when there are no files.
std::string encode_files_message(const std::vector<std::string>& files) {
    if (files.empty()) {
        return "";
    }
    std::string msg = "FILES";
    for (const auto& f : files) {
        msg += "\n";
        msg += f;
    }
    return msg;
}

/// Print help message to stdout.
void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options] [path...]\n"
              << "\n"
              << "CLI launcher for ImNeovim.\n"
              << "\n"
              << "Options:\n"
              << "  --help, -h     Show this help message and exit.\n"
              << "  --version, -v  Show version information and exit.\n"
              << "\n"
              << "Positional arguments:\n"
              << "  <folder>       Open the folder in the workspace.\n"
              << "  <file>         Open the file for editing.\n"
              << "\n"
              << "If an imnvim instance is already open for the same folders,\n"
              << "it will be activated and any files will be opened there.\n";
}

/// Print version information to stdout.
void print_version() {
    std::cout << "imnv version " << IMNVIM_VERSION << "\n"
              << "Git SHA:     " << IMNVIM_GIT_SHA << "\n"
              << "Build date:  " << IMNVIM_BUILD_DATE << "\n"
              << "Compiler:    " << IMNVIM_COMPILER_ID << " "
              << IMNVIM_COMPILER_VERSION << "\n";
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

/// Spawn imnvim as a detached child process, passing each string in
/// `args` as a separate argv entry after the program path.
/// Returns true on success.
bool spawn_imnvim(const std::string& imnvim_path,
                  const std::vector<std::string>& args) {
#ifdef _WIN32
    std::wstring wpath(imnvim_path.begin(), imnvim_path.end());
    std::wstring wargs = L"\"" + wpath + L"\"";
    for (const auto& a : args) {
        std::wstring wa(a.begin(), a.end());
        wargs += L" \"" + wa + L"\"";
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
    for (const auto& a : args) {
        argv.push_back(a.c_str());
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
    // -- Phase 1: Parse flags and classify positional arguments -----------
    ParsedArgs parsed;
    bool saw_double_dash = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};

        // "--" marks the end of options; everything after is a path.
        if (!saw_double_dash && arg == "--") {
            saw_double_dash = true;
            continue;
        }

        if (!saw_double_dash) {
            if (arg == "--help" || arg == "-h") {
                print_help(argv[0]);
                return 0;
            }
            if (arg == "--version" || arg == "-v") {
                print_version();
                return 0;
            }
            if (arg.starts_with("--")) {
                std::cerr << "imnv: unknown option '" << arg << "'\n";
                std::cerr << "Try '" << argv[0]
                          << " --help' for more information.\n";
                return 1;
            }
        }

        // Positional argument — classify as folder or file.
        auto kind = classify_path(arg);
        switch (kind) {
        case PathKind::Folder: {
            auto abs =
                std::filesystem::weakly_canonical(std::filesystem::path{arg});
            parsed.folders.push_back(path_to_utf8(abs));
            break;
        }
        case PathKind::File: {
            auto abs =
                std::filesystem::weakly_canonical(std::filesystem::path{arg});
            parsed.files.push_back(path_to_utf8(abs));
            break;
        }
        case PathKind::Invalid:
            std::cerr << "imnv: path does not exist or is not a "
                         "file/directory: '"
                      << arg << "'\n";
            return 1;
        }
    }

    // -- Phase 2: Try to activate an existing instance via IPC ------------
    std::string key = derive_key(parsed.folders);

    if (!key.empty()) {
        auto ipc = ImApp::IpcChannel::create();
        std::string msg = encode_files_message(parsed.files);
        if (ipc->send_message(key, msg)) {
            return 0;
        }
    }

    // -- Phase 3: No existing instance — spawn imnvim ---------------------
    std::vector<std::string> spawn_args;
    spawn_args.insert(spawn_args.end(),
                      std::make_move_iterator(parsed.folders.begin()),
                      std::make_move_iterator(parsed.folders.end()));
    spawn_args.insert(spawn_args.end(),
                      std::make_move_iterator(parsed.files.begin()),
                      std::make_move_iterator(parsed.files.end()));

    std::string imnvim_path = get_imnvim_path();
    if (!spawn_imnvim(imnvim_path, spawn_args)) {
        // Fallback: try PATH lookup.
        spawn_imnvim("imnvim", spawn_args);
    }

    return 0;
}
