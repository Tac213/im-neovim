#include "im_app/application.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#if defined(IM_APP_NO_CONSOLE)
_Use_decl_annotations_ int WINAPI
WinMain(HINSTANCE instance,
        HINSTANCE hPrevInstance, // NOLINT(readability-identifier-naming)
        LPSTR lpCmdLine,         // NOLINT(readability-identifier-naming)
        int nShowCmd)            // NOLINT(readability-identifier-naming)
#else
int main(int argc, char** argv)
#endif
{
#if defined(IM_APP_NO_CONSOLE)
    int argc = __argc;
    char** argv = __argv;
#endif
    auto* app = ImApp::create_im_app(argc, argv);
    auto returncode = app->exec();
    delete app;
    return returncode;
}
