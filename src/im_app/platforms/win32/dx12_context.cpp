#include "dx12_context.h"
#include "dx_helper.h"
#include "win32_window.h"
#if defined(IM_APP_DEBUG)
#include <dxgidebug.h>
#endif

namespace ImApp {
#if defined(IM_APP_DEBUG)
static void debug_message_callback(D3D12_MESSAGE_CATEGORY category,
                                   D3D12_MESSAGE_SEVERITY severity,
                                   D3D12_MESSAGE_ID message_id,
                                   const char* description, void* context) {
    switch (severity) {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        fprintf(stderr, "[D3D12 CORRUPTION] %s\n", description);
        break;
    case D3D12_MESSAGE_SEVERITY_ERROR:
        fprintf(stderr, "[D3D12 ERROR] %s\n", description);
        break;
    case D3D12_MESSAGE_SEVERITY_WARNING:
        fprintf(stderr, "[D3D12 WARNING] %s\n", description);
        break;
    case D3D12_MESSAGE_SEVERITY_INFO:
        fprintf(stdout, "[D3D12 INFO] %s\n", description);
        break;
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
        fprintf(stdout, "[D3D12 MESSAGE] %s\n", description);
        break;
    }
}
#endif

static void get_hardware_adapter(IDXGIFactory1* factory,
                                 IDXGIAdapter1** adapter,
                                 bool request_high_performance_adapter);

D3D12Context::D3D12Context(std::shared_ptr<Window> window)
    : m_use_warp_device(false) {
    auto win32_win = std::static_pointer_cast<Win32Window>(window);
    m_hwnd = win32_win->get_hwnd();
}

void D3D12Context::initialize() { _load_pipeline(); }

void D3D12Context::finalize() {
    _cleanup_render_target();
    if (m_fence_event) {
        ::CloseHandle(m_fence_event);
    }
    m_fence.Reset();
    m_command_list.Reset();
    for (auto& frame_context : m_frame_contexts) {
        frame_context.command_allocator.Reset();
    }
    m_rtv_heap.Reset();
    m_srv_heap.Reset();
    m_srv_heap_allocator.destroy();
    if (m_swap_chain_waitable_object) {
        ::CloseHandle(m_swap_chain_waitable_object);
    }
    m_swap_chain.Reset();
    m_command_queue.Reset();
    m_device.Reset();
#if defined(IM_APP_DEBUG)
    IDXGIDebug1* p_debug = nullptr;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&p_debug)))) {
        p_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
        p_debug->Release();
    }
#endif
}

void D3D12Context::swap_buffers() {
    m_swap_chain_occluded = false;
    HRESULT hr = m_swap_chain->Present(1, 0); // Present with vsync
    m_swap_chain_occluded = (hr == DXGI_STATUS_OCCLUDED);
    m_frame_index++;
}

void D3D12Context::on_frame_buffer_size_changed(uint32_t width,
                                                uint32_t height) {
    _cleanup_render_target();
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    m_swap_chain->GetDesc1(&desc);
    throw_if_failed(
        m_swap_chain->ResizeBuffers(0, width, height, desc.Format, desc.Flags));
    _create_render_target();
}

void D3D12Context::_load_pipeline() {
    uint32_t dxgi_factory_flags = 0;
#if defined(IM_APP_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the
    // active device.
    ComPtr<ID3D12Debug> debug_controller;
    {
        if (SUCCEEDED(
                D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
            debug_controller->EnableDebugLayer();
            ComPtr<ID3D12Debug1> debug_controller1;
            debug_controller->QueryInterface(IID_PPV_ARGS(&debug_controller1));
            if (debug_controller1) {
                debug_controller1->SetEnableGPUBasedValidation(true);
            }

            // Enable additional debug layers.
            dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif
    ComPtr<IDXGIFactory4> factory;
    throw_if_failed(
        CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory)));

    if (m_use_warp_device) {
        ComPtr<IDXGIAdapter> warp_adapter;
        throw_if_failed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)));

        throw_if_failed(D3D12CreateDevice(warp_adapter.Get(),
                                          D3D_FEATURE_LEVEL_11_0,
                                          IID_PPV_ARGS(&m_device)));
    } else {
        ComPtr<IDXGIAdapter1> hardware_adapter;
        get_hardware_adapter(factory.Get(), &hardware_adapter, true);

        throw_if_failed(D3D12CreateDevice(hardware_adapter.Get(),
                                          D3D_FEATURE_LEVEL_11_0,
                                          IID_PPV_ARGS(&m_device)));
    }
#if defined(IM_APP_DEBUG)
    if (debug_controller) {
        ComPtr<ID3D12InfoQueue1> p_info_queue;
        if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&p_info_queue)))) {
            // Suppress whole categories of messages
            // D3D12_MESSAGE_CATEGORY categories[] = {};

            // Suppress messages based on their severity level
            D3D12_MESSAGE_SEVERITY severities[] = {
                D3D12_MESSAGE_SEVERITY_INFO //
            };

            // Suppress individual messages by their ID
            D3D12_MESSAGE_ID deny_ids[] = {
                // D3D12 WARNING: ID3D12CommandList::ClearRenderTargetView: The
                // clear values do not match those passed to resource creation.
                // The clear operation is typically slower as a result; but will
                // still clear to the desired value. [ EXECUTION WARNING #820:
                // CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE]
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,

                // D3D12 WARNING: ID3D12CommandList::ClearDepthStencilView: The
                // clear values do not match those passed to resource creation.
                // The clear operation is typically slower as a result; but will
                // still clear to the desired value. [ EXECUTION WARNING #821:
                // CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE]
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE //
            };

            D3D12_INFO_QUEUE_FILTER new_filter = {};
            // new_filter.DenyList.NumCategories = _countof(categories);
            // new_filter.DenyList.pCategoryList = categories;
            new_filter.DenyList.NumSeverities = _countof(severities);
            new_filter.DenyList.pSeverityList = severities;
            new_filter.DenyList.NumIDs = _countof(deny_ids);
            new_filter.DenyList.pIDList = deny_ids;

            throw_if_failed(p_info_queue->PushStorageFilter(&new_filter));
            throw_if_failed(p_info_queue->SetBreakOnSeverity(
                D3D12_MESSAGE_SEVERITY_ERROR, true));
            throw_if_failed(p_info_queue->SetBreakOnSeverity(
                D3D12_MESSAGE_SEVERITY_CORRUPTION, true));
            throw_if_failed(p_info_queue->SetBreakOnSeverity(
                D3D12_MESSAGE_SEVERITY_WARNING, true));

            DWORD callback_cookie;
            p_info_queue->RegisterMessageCallback(
                debug_message_callback, D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                nullptr, &callback_cookie);
        }
    }
#endif

    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
        rtv_heap_desc.NumDescriptors = g_back_buffers_count;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtv_heap_desc.NodeMask = 1;
        throw_if_failed(m_device->CreateDescriptorHeap(
            &rtv_heap_desc, IID_PPV_ARGS(&m_rtv_heap)));

        m_rtv_descriptor_size = m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
            m_rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (auto& rt_descriptor : m_main_render_target_descriptors) {
            rt_descriptor = rtv_handle;
            rtv_handle.ptr += m_rtv_descriptor_size;
        }
    }

    {
        // Describe and create a shader resource view (SRV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
        srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.NumDescriptors = g_srv_heap_size;
        srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        throw_if_failed(m_device->CreateDescriptorHeap(
            &srv_heap_desc, IID_PPV_ARGS(&m_srv_heap)));
        m_srv_heap_allocator.create(m_device, m_srv_heap);
    }

    {
        // Describe and create the command queue.
        D3D12_COMMAND_QUEUE_DESC queue_desc = {};
        queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_desc.NodeMask = 1;

        throw_if_failed(m_device->CreateCommandQueue(
            &queue_desc, IID_PPV_ARGS(&m_command_queue)));
    }

    for (auto& frame_context : m_frame_contexts) {
        throw_if_failed(m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&frame_context.command_allocator)));
    }

    // Create the command list.
    throw_if_failed(
        m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    m_frame_contexts[0].command_allocator.Get(),
                                    nullptr, IID_PPV_ARGS(&m_command_list)));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    throw_if_failed(m_command_list->Close());

    throw_if_failed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                          IID_PPV_ARGS(&m_fence)));
    // Create an event handle to use for frame synchronization.
    m_fence_event = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);

    {
        // Describe and create the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
        ZeroMemory(&swap_chain_desc, sizeof(swap_chain_desc));
        swap_chain_desc.BufferCount = g_back_buffers_count;
        swap_chain_desc.Width = 0;
        swap_chain_desc.Height = 0;
        swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_chain_desc.Flags =
            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.SampleDesc.Count = 1;
        swap_chain_desc.SampleDesc.Quality = 0;
        swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
        swap_chain_desc.Stereo = FALSE;

        ComPtr<IDXGIFactory5> factory5 = nullptr;
        ComPtr<IDXGISwapChain1> swap_chain_1 = nullptr;
        throw_if_failed(CreateDXGIFactory1(IID_PPV_ARGS(&factory5)));
        BOOL allow_tearing = FALSE;
        factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                      &allow_tearing, sizeof(allow_tearing));
        m_swap_chain_tearing_support = (allow_tearing == TRUE);
        if (m_swap_chain_tearing_support) {
            swap_chain_desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }
        throw_if_failed(factory5->CreateSwapChainForHwnd(
            m_command_queue.Get(), m_hwnd, &swap_chain_desc, nullptr, nullptr,
            &swap_chain_1));
        throw_if_failed(swap_chain_1.As(&m_swap_chain));
        if (m_swap_chain_tearing_support) {
            factory5->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
        }
        m_swap_chain->SetMaximumFrameLatency(g_back_buffers_count);
        m_swap_chain_waitable_object =
            m_swap_chain->GetFrameLatencyWaitableObject();
    }

    _create_render_target();
}

void D3D12Context::_create_render_target() {
    for (uint32_t i = 0; i < g_back_buffers_count; i++) {
        ComPtr<ID3D12Resource> p_back_buffer = nullptr;
        m_swap_chain->GetBuffer(i, IID_PPV_ARGS(&p_back_buffer));
        m_device->CreateRenderTargetView(p_back_buffer.Get(), nullptr,
                                         m_main_render_target_descriptors[i]);
        m_main_render_target_resources[i] = p_back_buffer;
    }
}

void D3D12Context::wait_for_pending_operations() {
    m_command_queue->Signal(m_fence.Get(), ++m_fence_last_signaled_value);
    throw_if_failed(m_fence->SetEventOnCompletion(m_fence_last_signaled_value,
                                                  m_fence_event));
    ::WaitForSingleObject(m_fence_event, INFINITE);
}

FrameContext* D3D12Context::wait_for_next_frame_context() {
    FrameContext* frame_context =
        &m_frame_contexts[m_frame_index % g_frames_in_flight_count];
    if (m_fence->GetCompletedValue() < frame_context->fence_value) {
        m_fence->SetEventOnCompletion(frame_context->fence_value,
                                      m_fence_event);
        HANDLE waitable_objects[] = {m_swap_chain_waitable_object,
                                     m_fence_event};
        ::WaitForMultipleObjects(_countof(waitable_objects), waitable_objects,
                                 true, INFINITE);
    } else {
        ::WaitForSingleObject(m_swap_chain_waitable_object, INFINITE);
    }
    return frame_context;
}

void D3D12Context::signal_command_queue(FrameContext* frame_context) {
    m_command_queue->Signal(m_fence.Get(), ++m_fence_last_signaled_value);
    frame_context->fence_value = m_fence_last_signaled_value;
}

std::pair<D3D12_CPU_DESCRIPTOR_HANDLE*, ComPtr<ID3D12Resource>>
D3D12Context::get_back_buffer() {
    uint32_t back_buffer_idx = m_swap_chain->GetCurrentBackBufferIndex();
    return {&m_main_render_target_descriptors[back_buffer_idx],
            m_main_render_target_resources[back_buffer_idx]};
}

void D3D12Context::_cleanup_render_target() {
    wait_for_pending_operations();

    for (auto& rt_resource : m_main_render_target_resources) {
        if (rt_resource) {
            // rt_resource->Release();
            rt_resource = nullptr;
        }
    }
}

void get_hardware_adapter(IDXGIFactory1* factory, IDXGIAdapter1** adapter,
                          bool request_high_performance_adapter) {
    *adapter = nullptr;

    ComPtr<IDXGIAdapter1> adapter1;

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
        for (UINT adapter_index = 0;
             SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                 adapter_index,
                 request_high_performance_adapter == true
                     ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                     : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                 IID_PPV_ARGS(&adapter1)));
             ++adapter_index) {
            DXGI_ADAPTER_DESC1 desc;
            adapter1->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the
                // command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't
            // create the actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter1.Get(),
                                            D3D_FEATURE_LEVEL_11_0,
                                            _uuidof(ID3D12Device), nullptr))) {
                break;
            }
        }
    }

    if (adapter1.Get() == nullptr) {
        for (UINT adapter_index = 0;
             SUCCEEDED(factory->EnumAdapters1(adapter_index, &adapter1));
             ++adapter_index) {
            DXGI_ADAPTER_DESC1 desc;
            adapter1->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the
                // command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't
            // create the actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter1.Get(),
                                            D3D_FEATURE_LEVEL_11_0,
                                            _uuidof(ID3D12Device), nullptr))) {
                break;
            }
        }
    }

    *adapter = adapter1.Detach();
}

void DescriptorHeapAllocator::create(ComPtr<ID3D12Device> device,
                                     ComPtr<ID3D12DescriptorHeap> in_heap) {
    if (heap || !free_indices.empty()) {
        return;
    }
    heap = in_heap;
    D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
    heap_type = desc.Type;
    heap_start_cpu = heap->GetCPUDescriptorHandleForHeapStart();
    heap_start_gpu = heap->GetGPUDescriptorHandleForHeapStart();
    heap_handle_increment = device->GetDescriptorHandleIncrementSize(heap_type);
    free_indices.reserve(desc.NumDescriptors);
    for (int32_t n = desc.NumDescriptors; n > 0; n--) {
        free_indices.push_back(n - 1);
    }
}

void DescriptorHeapAllocator::destroy() {
    heap.Reset();
    free_indices.clear();
}

void DescriptorHeapAllocator::alloc(
    D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle,
    D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle) {
    if (free_indices.size() <= 0) {
        return;
    }
    int32_t idx = free_indices.back();
    free_indices.pop_back();
    out_cpu_desc_handle->ptr =
        heap_start_cpu.ptr + (idx * heap_handle_increment);
    out_gpu_desc_handle->ptr =
        heap_start_gpu.ptr + (idx * heap_handle_increment);
}

void DescriptorHeapAllocator::free(
    D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle,
    D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle) {
    int32_t cpu_idx = static_cast<int32_t>(
        (out_cpu_desc_handle.ptr - heap_start_cpu.ptr) / heap_handle_increment);
    int32_t gpu_idx = static_cast<int32_t>(
        (out_gpu_desc_handle.ptr - heap_start_gpu.ptr) / heap_handle_increment);
    free_indices.push_back(cpu_idx);
}
} // namespace ImApp
