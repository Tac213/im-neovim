#include "darwin_metal_imgui_renderer.h"
#include "metal_context.h"
#include <imgui_impl_glfw.h>
#include <imgui_impl_metal.h>

namespace ImApp {
DarwinMetalImGuiRenderer::DarwinMetalImGuiRenderer(
    std::shared_ptr<DarwinWindow> window)
    : m_window(window), m_render_pass_descriptor(nil), m_drawable(nil),
      m_command_buffer(nil), m_render_encoder(nil) {
    _initialize();
}

DarwinMetalImGuiRenderer::~DarwinMetalImGuiRenderer() { _finalize(); }

void DarwinMetalImGuiRenderer::new_frame() {
    auto context = MetalContext::get();
    IM_ASSERT(context);
    IM_ASSERT(m_window);
    uint32_t width = m_window->get_width();
    uint32_t height = m_window->get_height();
    CAMetalLayer *layer = context->get_layer();
    id<MTLCommandQueue> command_queue = context->get_command_queue();
    layer.drawableSize = CGSizeMake(width, height);
    m_drawable = [layer nextDrawable];

    m_command_buffer = [command_queue commandBuffer];
    m_render_pass_descriptor.colorAttachments[0].clearColor =
        MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    m_render_pass_descriptor.colorAttachments[0].texture = m_drawable.texture;
    m_render_pass_descriptor.colorAttachments[0].loadAction =
        MTLLoadActionClear;
    m_render_pass_descriptor.colorAttachments[0].storeAction =
        MTLStoreActionStore;
    m_render_encoder = [m_command_buffer
        renderCommandEncoderWithDescriptor:m_render_pass_descriptor];
    [m_render_encoder pushDebugGroup:@"Im App"];

    // Start the Dear ImGui frame
    ImGui_ImplMetal_NewFrame(m_render_pass_descriptor);
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void DarwinMetalImGuiRenderer::render(std::shared_ptr<Window> &window) {
    // Rendering
    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), m_command_buffer,
                                   m_render_encoder);

    // Update and Render additional Platform Windows
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    [m_render_encoder popDebugGroup];
    [m_render_encoder endEncoding];

    [m_command_buffer presentDrawable:m_drawable];
    [m_command_buffer commit];

    m_render_encoder = nil;
    m_command_buffer = nil;
    m_drawable = nil;
}

void DarwinMetalImGuiRenderer::_initialize() {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
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

    // Setup scaling
    float main_scale =
        ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(
        main_scale); // Bake a fixed style scale. (until we have a solution for
                     // dynamic style scaling, changing this requires resetting
                     // Style + calling this again)
    style.FontScaleDpi =
        main_scale; // Set initial font scale. (using
                    // io.ConfigDpiScaleFonts=true makes this unnecessary. We
                    // leave both here for documentation purpose)
    io.ConfigDpiScaleFonts =
        true; // [Experimental] Automatically overwrite style.FontScaleDpi in
              // Begin() when Monitor DPI changes. This will scale fonts but
              // _NOT_ scale sizes/padding for now.
    io.ConfigDpiScaleViewports =
        true; // [Experimental] Scale Dear ImGui and Platform Windows when
              // Monitor DPI changes.

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform
    // windows can look identical to regular ones.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    auto context = MetalContext::get();
    IM_ASSERT(context);
    IM_ASSERT(m_window);
    ImGui_ImplGlfw_InitForOther(m_window->get_glfw_window(), true);
    id<MTLDevice> device = context->get_device();
    ImGui_ImplMetal_Init(device);

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
    // style.FontSizeBase = 20.0f;
    // io.Fonts->AddFontDefault();
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    // ImFont* font =
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    // IM_ASSERT(font != nullptr);

    m_render_pass_descriptor = [MTLRenderPassDescriptor new];
}

void DarwinMetalImGuiRenderer::_finalize() {
    // Cleanup
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_render_encoder = nil;
    m_command_buffer = nil;
    m_drawable = nil;
    m_render_pass_descriptor = nil;
    m_window.reset();
}

std::shared_ptr<ImGuiRenderer>
ImGuiRenderer::create(std::shared_ptr<Window> window, GraphicsBackend backend) {
    auto darwin_window = std::static_pointer_cast<DarwinWindow>(window);
    auto renderer = std::make_shared<DarwinMetalImGuiRenderer>(darwin_window);
    return renderer;
}
} // namespace ImApp
