#include "win32_opengl_imgui_renderer.h"
#include "im_app/window.h"
#include "wgl_context.h"
#include <GL/glew.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_win32.h>

namespace ImApp {
Win32OpenGLImGuiRenderer::Win32OpenGLImGuiRenderer() { _initialize(); }

Win32OpenGLImGuiRenderer::~Win32OpenGLImGuiRenderer() { _finalize(); }

void Win32OpenGLImGuiRenderer::new_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Win32OpenGLImGuiRenderer::render(std::shared_ptr<Window>& window) {
    if (!window) {
        return;
    }
    ImGui::Render();
    glViewport(0, 0, static_cast<GLsizei>(window->get_width()),
               static_cast<GLsizei>(window->get_height()));
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Update and Render additional Platform Windows
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        // Restore the OpenGL rendering context to the main window DC, since
        // platform windows might have changed it.
        auto context = WGLContext::get();
        IM_ASSERT(context);
        context->make_current();
    }
}

void Win32OpenGLImGuiRenderer::
    _initialize() { // NOLINT(readability-convert-member-functions-to-static)
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableGamepad;            // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport
                                                        // / Platform Windows
    io.IniFilename = nullptr;                           // Disable imgui.ini

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform
    // windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    auto context = WGLContext::get();
    IM_ASSERT(context);
    ImGui_ImplWin32_InitForOpenGL(context->get_hwnd());
    ImGui_ImplOpenGL3_Init();

    // Win32+GL needs specific hooks for viewport, as there are specific things
    // needed to tie Win32 and GL api.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        IM_ASSERT(platform_io.Renderer_CreateWindow == nullptr);
        IM_ASSERT(platform_io.Renderer_DestroyWindow == nullptr);
        IM_ASSERT(platform_io.Renderer_SwapBuffers == nullptr);
        IM_ASSERT(platform_io.Platform_RenderWindow == nullptr);
        platform_io.Renderer_CreateWindow = [](ImGuiViewport* viewport) {
            if (viewport->RendererUserData != nullptr) {
#if defined(_DEBUG)
                fprintf(stderr,
                        "ImGuiViewport->RendererUserData is not nullptr!!!");
#endif
                return;
            }
            WGLWindowData* data = IM_NEW(WGLWindowData);
            auto context = WGLContext::get();
            IM_ASSERT(context);
            context->create_device(
                reinterpret_cast<HWND>(viewport->PlatformHandle), data->hdc);
            viewport->RendererUserData = data;
        };
        platform_io.Renderer_DestroyWindow = [](ImGuiViewport* viewport) {
            if (viewport->RendererUserData != nullptr) {
                WGLWindowData* data = reinterpret_cast<WGLWindowData*>(
                    viewport->RendererUserData);
                WGLContext::cleanup_device(
                    reinterpret_cast<HWND>(viewport->PlatformHandle),
                    data->hdc);
                viewport->RendererUserData = nullptr;
            }
        };
        platform_io.Renderer_SwapBuffers = [](ImGuiViewport* viewport, void*) {
            if (WGLWindowData* data = reinterpret_cast<WGLWindowData*>(
                    viewport->RendererUserData)) {
                WGLContext::swap_buffers(data->hdc);
            }
        };
        platform_io.Platform_RenderWindow = [](ImGuiViewport* viewport, void*) {
            if (WGLWindowData* data = reinterpret_cast<WGLWindowData*>(
                    viewport->RendererUserData)) {
                auto context = WGLContext::get();
                IM_ASSERT(context);
                context->make_current(data->hdc);
            }
        };
    }

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can
    // also load multiple fonts and use ImGui::PushFont()/PopFont() to select
    // them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you
    // need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please
    // handle those errors in your application (e.g. use an assertion, or
    // display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and
    // stored into a texture when calling
    // ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame
    // below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use
    // Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string
    // literal you need to write a double backslash \\ !
    // io.Fonts->AddFontDefault();
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    // ImFont* font =
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f,
    // NULL, io.Fonts->GetGlyphRangesJapanese()); IM_ASSERT(font != nullptr);
}

void Win32OpenGLImGuiRenderer::
    _finalize() // NOLINT(readability-convert-member-functions-to-static)
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

std::shared_ptr<ImGuiRenderer> ImGuiRenderer::create() {
    auto renderer = std::make_shared<Win32OpenGLImGuiRenderer>();
    return renderer;
}
} // namespace ImApp
