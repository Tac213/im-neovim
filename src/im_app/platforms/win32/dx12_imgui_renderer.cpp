#include "dx12_imgui_renderer.h"
#include "dx12_context.h"
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

namespace ImApp {
D3D12ImGuiRenderer::D3D12ImGuiRenderer() { _initialize(); }

D3D12ImGuiRenderer::~D3D12ImGuiRenderer() { _finalize(); }

void D3D12ImGuiRenderer::new_frame() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void D3D12ImGuiRenderer::render(std::shared_ptr<Window>& window) {
    ImGui::Render();

    auto context = D3D12Context::get();
    IM_ASSERT(context);

    auto* frame_context = context->wait_for_next_frame_context();
    frame_context->command_allocator->Reset();

    auto [back_buffer_descriptor, back_buffer_resource] =
        context->get_back_buffer();
    auto command_queue = context->get_command_queue();
    auto command_list = context->get_graphics_command_list();
    auto srv_heap = context->get_srv_heap();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = back_buffer_resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list->Reset(frame_context->command_allocator.Get(), nullptr);
    command_list->ResourceBarrier(1, &barrier);

    // Render Dear ImGui graphics
    const float clear_color_with_alpha[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    command_list->ClearRenderTargetView(*back_buffer_descriptor,
                                        clear_color_with_alpha, 0, nullptr);
    command_list->OMSetRenderTargets(1, back_buffer_descriptor, FALSE, nullptr);
    ID3D12DescriptorHeap* descriptor_heaps[] = {srv_heap.Get()};
    command_list->SetDescriptorHeaps(_countof(descriptor_heaps),
                                     descriptor_heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list.Get());
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    command_list->ResourceBarrier(1, &barrier);
    command_list->Close();

    ID3D12CommandList* command_lists[] = {command_list.Get()};
    command_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    // Update and Render additional Platform Windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    context->signal_command_queue(frame_context);
}

void D3D12ImGuiRenderer::
    _initialize() { // NOLINT(readability-convert-member-functions-to-static)
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

    auto context = D3D12Context::get();
    IM_ASSERT(context);
    float main_scale = ImGui_ImplWin32_GetDpiScaleForHwnd(context->get_hwnd());

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
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
    ImGui_ImplWin32_Init(context->get_hwnd());

    auto device = context->get_device();
    auto command_queue = context->get_command_queue();
    auto srv_heap = context->get_srv_heap();
    auto* srv_heap_allocator = context->get_srv_heap_allocator();
    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = device.Get();
    init_info.CommandQueue = command_queue.Get();
    init_info.NumFramesInFlight = D3D12Context::get_num_frames_in_flight();
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = srv_heap.Get();
    init_info.SrvDescriptorAllocFn =
        [](ImGui_ImplDX12_InitInfo* info,
           D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
           D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) {
            auto* allocator =
                reinterpret_cast<DescriptorHeapAllocator*>(info->UserData);
            allocator->alloc(out_cpu_handle, out_gpu_handle);
        };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
        auto* allocator =
            reinterpret_cast<DescriptorHeapAllocator*>(info->UserData);
        allocator->free(cpu_handle, gpu_handle);
    };
    init_info.UserData = srv_heap_allocator;
    ImGui_ImplDX12_Init(&init_info);

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
}

void D3D12ImGuiRenderer::
    _finalize() { // NOLINT(readability-convert-member-functions-to-static)
    auto context = D3D12Context::get();
    IM_ASSERT(context);
    context->wait_for_pending_operations();

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
} // namespace ImApp
