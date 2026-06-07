#include "im_app/application.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace {

// Convert a wide (UTF-16) string to UTF-8.
std::string _wide_to_utf8(const wchar_t* wstr) {
    if (wstr == nullptr || wstr[0] == L'\0') {
        return {};
    }
    int size_needed = ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0,
                                            nullptr, nullptr);
    if (size_needed <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size_needed - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), size_needed,
                          nullptr, nullptr);
    return result;
}

} // anonymous namespace

#if defined(IM_APP_NO_CONSOLE)
_Use_decl_annotations_ int WINAPI
WinMain(HINSTANCE instance,
        HINSTANCE hPrevInstance, // NOLINT(readability-identifier-naming)
        LPSTR lpCmdLine,         // NOLINT(readability-identifier-naming)
        int nShowCmd)            // NOLINT(readability-identifier-naming)
#else
int main()
#endif
{
    // Always parse the Unicode command line so non-ASCII install paths
    // and arguments are preserved — avoids the ANSI truncation inherent
    // in __argc/__argv or WinMain's LPSTR lpCmdLine.
    int argc = 0;
    LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

    // Convert wide argv to UTF-8 for create_im_app (signature unchanged).
    std::vector<std::string> utf8_strings;
    std::vector<char*> utf8_argv;
    if (wargv != nullptr) {
        utf8_strings.reserve(argc);
        utf8_argv.reserve(static_cast<size_t>(argc) + 1);
        for (int i = 0; i < argc; ++i) {
            utf8_strings.push_back(_wide_to_utf8(wargv[i]));
            utf8_argv.push_back(utf8_strings.back().data());
        }
    }
    utf8_argv.push_back(nullptr); // argv terminator

    auto* app = ImApp::create_im_app(argc, utf8_argv.data());
    if (!app) {
        return 0;
    }
    auto returncode = app->exec();
    delete app;

    if (wargv != nullptr) {
        ::LocalFree(wargv);
    }

    return returncode;
}
