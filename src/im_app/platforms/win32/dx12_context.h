#pragma once

#include "im_app/graphics_context.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <DirectXMath.h>
#include <Windows.h>
#include <directx/d3dx12.h>
#include <dxgi1_6.h>
#include <wrl.h>

namespace ImApp {
using Microsoft::WRL::ComPtr;

struct FrameContext {
    ComPtr<ID3D12CommandAllocator> command_allocator = nullptr;
    uint64_t fence_value;
};

struct DescriptorHeapAllocator {
    ComPtr<ID3D12DescriptorHeap> heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE heap_start_cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE heap_start_gpu;
    uint32_t heap_handle_increment;
    std::vector<int32_t> free_indices;

    void create(ComPtr<ID3D12Device> device,
                ComPtr<ID3D12DescriptorHeap> in_heap);
    void destroy();
    void alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle,
               D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle);
    void free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle,
              D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle);
};

class D3D12Context : public GraphicsContext {
  public:
    explicit D3D12Context(std::shared_ptr<Window> window);

    virtual void initialize() override;
    virtual void finalize() override;
    virtual void swap_buffers() override;

    HWND get_hwnd() const { return m_hwnd; }
    ComPtr<ID3D12Device> get_device() const { return m_device; }
    ComPtr<ID3D12CommandQueue> get_command_queue() const {
        return m_command_queue;
    }
    ComPtr<ID3D12GraphicsCommandList> get_graphics_command_list() {
        return m_command_list;
    }
    ComPtr<ID3D12DescriptorHeap> get_srv_heap() const { return m_srv_heap; }
    DescriptorHeapAllocator* get_srv_heap_allocator() {
        return &m_srv_heap_allocator;
    }
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE*, ComPtr<ID3D12Resource>>
    get_back_buffer();
    void create_render_target();
    void wait_for_pending_operations();
    FrameContext* wait_for_next_frame_context();
    void signal_command_queue(FrameContext* frame_context);

    static std::shared_ptr<D3D12Context> get();
    static uint32_t get_num_frames_in_flight() {
        return g_frames_in_flight_count;
    }

  private:
    static const uint32_t g_frames_in_flight_count = 2;
    static const uint32_t g_back_buffers_count = 2;
    static const uint32_t g_srv_heap_size = 64;
    size_t m_frame_index = 0;
    bool m_use_warp_device = false;
    bool m_swap_chain_tearing_support = false;
    bool m_swap_chain_occluded = false;
    ComPtr<ID3D12Device> m_device = nullptr;
    ComPtr<IDXGISwapChain3> m_swap_chain = nullptr;
    ComPtr<ID3D12CommandQueue> m_command_queue = nullptr;
    ComPtr<ID3D12DescriptorHeap> m_rtv_heap = nullptr;
    ComPtr<ID3D12DescriptorHeap> m_srv_heap = nullptr;
    ComPtr<ID3D12GraphicsCommandList> m_command_list;
    DescriptorHeapAllocator m_srv_heap_allocator;
    FrameContext m_frame_contexts[g_frames_in_flight_count] = {};
    ComPtr<ID3D12Resource>
        m_main_render_target_resources[g_back_buffers_count] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE
    m_main_render_target_descriptors[g_back_buffers_count] = {};
    uint32_t m_rtv_descriptor_size = 0;

    HWND m_hwnd = nullptr;

    HANDLE m_swap_chain_waitable_object = nullptr;
    HANDLE m_fence_event = nullptr;
    ComPtr<ID3D12Fence> m_fence;
    size_t m_fence_last_signaled_value = 0;

    void _load_pipeline();
    void _cleanup_render_target();
};

} // namespace ImApp
