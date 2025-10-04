#include "glfw_opengl_imgui_renderer.h"
#include "glfw_window.h"
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace ImApp {
GlfwOpenGLImGuiRenderer::GlfwOpenGLImGuiRenderer(
    std::shared_ptr<GlfwWindow> window) {
    _initialize(window);
}

GlfwOpenGLImGuiRenderer::~GlfwOpenGLImGuiRenderer() { _finalize(); }

void GlfwOpenGLImGuiRenderer::new_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GlfwOpenGLImGuiRenderer::render(std::shared_ptr<Window>& window) {
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
    // (Platform functions may change the current OpenGL context, so we
    // save/restore it to make it easier to paste this code elsewhere.
    //  For this specific demo app we could also call
    //  glfwMakeContextCurrent(window) directly)
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
}

void GlfwOpenGLImGuiRenderer::
    _initialize( // NOLINT(readability-convert-member-functions-to-static)
        std::shared_ptr<GlfwWindow> window) {
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
    // io.ConfigViewportsNoAutoMerge = true;
    // io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup scaling
    float main_scale =
        ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(
        main_scale); // Bake a fixed style scale. (until we have a solution for
                     // dynamic style scaling, changing this requires resetting
                     // Style + calling this again)
    style.FontScaleDpi =
        main_scale; // Set initial font scale. (using
                    // io.ConfigDpiScaleFonts=true makes this unnecessary. We
                    // leave both here for documentation purpose)
#if GLFW_VERSION_MAJOR >= 3 && GLFW_VERSION_MINOR >= 3
    io.ConfigDpiScaleFonts =
        true; // [Experimental] Automatically overwrite style.FontScaleDpi in
              // Begin() when Monitor DPI changes. This will scale fonts but
              // _NOT_ scale sizes/padding for now.
    io.ConfigDpiScaleViewports =
        true; // [Experimental] Scale Dear ImGui and Platform Windows when
              // Monitor DPI changes.
#endif

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform
    // windows can look identical to regular ones.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    IM_ASSERT(window);
    ImGui_ImplGlfw_InitForOpenGL(window->get_glfw_window(), true);
    const char* glsl_version = "#version 130";
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can
    // also load multiple fonts and use ImGui::PushFont()/PopFont() to select
    // them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you
    // need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr.
    // Please handle those errors in your application (e.g. use an assertion, or
    // display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use
    // Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details. If you like the
    // default font but want it to scale better, consider using the
    // 'ProggyVector' from the same author!
    // - Remember that in C/C++ if you want to include a backslash \ in a string
    // literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at
    // runtime from the "fonts/" folder. See Makefile.emscripten for details.
    // style.FontSizeBase = 20.0f;
    // io.Fonts->AddFontDefault();
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    // ImFont* font =
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    // IM_ASSERT(font != nullptr);
}
void GlfwOpenGLImGuiRenderer::
    _finalize() { // NOLINT(readability-convert-member-functions-to-static)
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

std::shared_ptr<ImGuiRenderer>
ImGuiRenderer::create(std::shared_ptr<Window> window) {
    auto glfw_window = std::static_pointer_cast<GlfwWindow>(window);
    auto renderer = std::make_shared<GlfwOpenGLImGuiRenderer>(glfw_window);
    return renderer;
}
} // namespace ImApp
