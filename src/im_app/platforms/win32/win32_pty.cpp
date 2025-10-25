#include "win32_pty.h"
#include <spdlog/spdlog.h>

namespace ImApp {
static void log_win32_error() {
    WCHAR* s_buf = nullptr; /* Free via LocalFree */
    DWORD err = ::GetLastError();
    if (err == 0) {
        return;
    }
    int len = ::FormatMessageW(
        /* Error API error */
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,                                        /* no message source */
        err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), /* Default language */
        reinterpret_cast<LPWSTR>(&s_buf), 0,            /* size not used */
        nullptr);                                       /* no args */
    if (len == 0) {
        spdlog::error("Windows Error {:x}", err);
    } else {
        /* remove trailing cr/lf and dots */
        while (len > 0 && (s_buf[len - 1] <= L' ' || s_buf[len - 1] == L'.')) {
            s_buf[--len] = L'\0';
        }
        spdlog::error(s_buf);
        ::LocalFree(s_buf);
    }
}

Win32PseudoTerminal::~Win32PseudoTerminal() {
    // Clean-up client app's process-info & thread
    if (m_cmd_pi.hThread != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_cmd_pi.hThread);
    }
    if (m_cmd_pi.hProcess != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_cmd_pi.hProcess);
    }
    // Close ConPTY - this will terminate client process if running
    if (m_h_pc != INVALID_HANDLE_VALUE) {
        ::ClosePseudoConsole(m_h_pc);
    }
    // Clean-up the pipes
    if (m_h_pipe_out != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_h_pipe_out);
    }
    if (m_h_pipe_in != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_h_pipe_in);
    }
}

bool Win32PseudoTerminal::launch(uint16_t row, uint16_t col) {
    if (is_valid()) {
        return true;
    }
    HANDLE h_console = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (h_console) {
        DWORD console_mode{};
        ::GetConsoleMode(h_console, &console_mode);
        if (!(console_mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            console_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            ::SetConsoleMode(h_console, console_mode);
        }
    }

    // Create the Pseudo Console and pipes to it
    HANDLE h_pipe_pty_in{INVALID_HANDLE_VALUE};
    HANDLE h_pipe_pty_out{INVALID_HANDLE_VALUE};

    // Create the pipes to which the ConPTY will connect
    if (!::CreatePipe(&h_pipe_pty_in, &m_h_pipe_out, nullptr, 0)) {
        spdlog::critical("Failed to create pipe.");
        log_win32_error();
        return false;
    }
    if (!::CreatePipe(&m_h_pipe_in, &h_pipe_pty_out, nullptr, 0)) {
        spdlog::critical("Failed to create pipe.");
        log_win32_error();
        return false;
    }
    // Create the Pseudo Console of the required size,
    // attached to the PTY-end of the pipes
    COORD console_size{
        .X = static_cast<SHORT>(col),
        .Y = static_cast<SHORT>(row),
    };
    if (FAILED(::CreatePseudoConsole(console_size, h_pipe_pty_in,
                                     h_pipe_pty_out, 0, &m_h_pc))) {
        spdlog::critical("Failed to create pseudo console.");
        log_win32_error();
        return false;
    }
    // Note: We can close the handles to the PTY-end of the pipes here
    // because the handles are dup'ed into the ConHost and will be released
    // when the ConPTY is destroyed.
    if (h_pipe_pty_out != INVALID_HANDLE_VALUE) {
        ::CloseHandle(h_pipe_pty_out);
    }
    if (h_pipe_pty_in != INVALID_HANDLE_VALUE) {
        ::CloseHandle(h_pipe_pty_in);
    }

    wchar_t cmd_path[MAX_PATH];
    if (!::GetEnvironmentVariableW(L"SystemRoot", cmd_path, MAX_PATH)) {
        wcscpy_s(cmd_path, L"C:\\WINDOWS");
    }
    wcscat_s(cmd_path, L"\\System32\\cmd.exe");

    STARTUPINFOEXW si;

    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = 0;
    si.StartupInfo.wShowWindow = 0;
    si.StartupInfo.hStdInput = nullptr;
    si.StartupInfo.hStdOutput = nullptr;
    si.StartupInfo.hStdError = nullptr;

    DWORD attribute_count = 1;
    SIZE_T attribute_list_size = 0;
    // Get how many bytes we need for the attribute list
    InitializeProcThreadAttributeList(nullptr, attribute_count, 0,
                                      &attribute_list_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attribute_list =
        static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            ::malloc(attribute_list_size));
    if (!InitializeProcThreadAttributeList(attribute_list, attribute_count, 0,
                                           &attribute_list_size)) {
        spdlog::critical("Failed to initialize process thread attribute list!");
        log_win32_error();
        if (attribute_list) {
            ::free(attribute_list);
        }
        return false;
    }
    // Set Pseudo Console attribute
    if (!::UpdateProcThreadAttribute(attribute_list, 0,
                                     PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                     m_h_pc, sizeof(HPCON), nullptr, nullptr)) {
        spdlog::critical("Failed to set pseudo console attribute.");
        log_win32_error();
        if (attribute_list) {
            ::DeleteProcThreadAttributeList(attribute_list);
            ::free(attribute_list);
        }
        return false;
    }
    si.lpAttributeList = attribute_list;

    m_cmd_pi.hProcess = INVALID_HANDLE_VALUE;
    m_cmd_pi.hThread = INVALID_HANDLE_VALUE;
    if (!::CreateProcessW(nullptr,  /* No module name - use Command Line */
                          cmd_path, /* Command Line */
                          nullptr, nullptr, FALSE, /* Inherit handles */
                          EXTENDED_STARTUPINFO_PRESENT,
                          nullptr, /* Use parent's environment block*/
                          nullptr, /* Use parent's starting directory*/
                          reinterpret_cast<LPSTARTUPINFOW>(&si), &m_cmd_pi)) {
        spdlog::critical(
            L"Failed to launch windows command prompt, path: '{}'.", cmd_path);
        log_win32_error();
        if (attribute_list) {
            ::DeleteProcThreadAttributeList(attribute_list);
            ::free(attribute_list);
        }
        return false;
    }

    if (attribute_list) {
        ::DeleteProcThreadAttributeList(attribute_list);
        ::free(attribute_list);
    }

    return true;
}

void Win32PseudoTerminal::terminate() {
    // Clean-up client app's process-info & thread
    if (m_cmd_pi.hThread != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_cmd_pi.hThread);
    }
    m_cmd_pi.hThread = INVALID_HANDLE_VALUE;
    if (m_cmd_pi.hProcess != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_cmd_pi.hProcess);
    }
    m_cmd_pi.hProcess = INVALID_HANDLE_VALUE;

    // Close ConPTY - this will terminate client process if running
    if (m_h_pc != INVALID_HANDLE_VALUE) {
        ::ClosePseudoConsole(m_h_pc);
    }
    m_h_pc = INVALID_HANDLE_VALUE;
    // Clean-up the pipes
    if (m_h_pipe_out != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_h_pipe_out);
    }
    m_h_pipe_out = INVALID_HANDLE_VALUE;
    if (m_h_pipe_in != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_h_pipe_in);
    }
    m_h_pipe_in = INVALID_HANDLE_VALUE;
}

bool Win32PseudoTerminal::is_valid() {
    return m_h_pc != INVALID_HANDLE_VALUE &&
           m_h_pipe_out != INVALID_HANDLE_VALUE &&
           m_h_pipe_in != INVALID_HANDLE_VALUE;
}

size_t Win32PseudoTerminal::write(const void* buff, size_t size) {
    if (m_h_pc == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (m_h_pipe_out == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD written;
    if (!::WriteFile(m_h_pipe_out, buff, size, &written, nullptr)) {
        log_win32_error();
        return 0;
    }
    return written;
}

size_t Win32PseudoTerminal::read(void* buff, size_t size) {
    if (m_h_pc == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (m_h_pipe_in == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD nread;
    if (!::ReadFile(m_h_pipe_in, buff, size, &nread, nullptr)) {
        log_win32_error();
        return 0;
    }
    return nread;
}

bool Win32PseudoTerminal::resize(uint16_t row, uint16_t col) {
    if (m_h_pc == INVALID_HANDLE_VALUE) {
        return false;
    }
    COORD console_size{
        .X = static_cast<SHORT>(col),
        .Y = static_cast<SHORT>(row),
    };
    return SUCCEEDED(::ResizePseudoConsole(m_h_pc, console_size));
}

std::shared_ptr<PseudoTerminal> PseudoTerminal::create() {
    return std::make_shared<Win32PseudoTerminal>();
}
} // namespace ImApp
