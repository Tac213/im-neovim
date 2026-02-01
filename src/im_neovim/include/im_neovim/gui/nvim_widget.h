#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <msgpack.hpp>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <uv.h>

namespace ImNeovim {
class NvimRequest;
class NvimWidget : public std::enable_shared_from_this<NvimWidget> {
  public:
    NvimWidget();
    ~NvimWidget();
    void open_file();
    std::shared_ptr<NvimRequest> start_nvim_request(
        const std::string& method, uint8_t param_count,
        std::function<void(msgpack::object&)>&& on_result,
        std::function<void(int32_t, const std::string&)>&& on_error);

  private:
    friend class NvimRequest;
    void _spawn_nvim();
    /* Callbacks by libuv */
    // Called by libuv when nvim exits/
    static void _on_nvim_exit(uv_process_t* nvim_proc, int64_t exit_status,
                              int term_signal);
    // Called by libuv to allocate memory for reading.
    static void _uv_alloc_cb(uv_handle_t* handle, size_t suggested,
                             uv_buf_t* buf);
    // Callback invoked by lib uv after it copies the data into the buffer
    // provided by `_uv_alloc_cb`. This is also called on EOF or when
    // `_uv_alloc_cb` returns a 0-lenth buffer.
    static void _uv_read_cb(uv_stream_t* stream, ssize_t nread,
                            const uv_buf_t* buf);
    static void _uv_write_cb(uv_write_t* req, int status);

    void _send_nvim_error(const msgpack::object& req, const std::string& msg);
    void _send_nvim_error(uint64_t msgid, const std::string& msg);
    void _handle_nvim_rpc(const std::vector<char>& msgpack_data);
    void _dispatch(msgpack::object& req);
    void _dispatch_request(msgpack::object& req);
    void _dispatch_response(msgpack::object& resp);
    void _dispatch_notification(msgpack::object& nt);

    uv_process_t m_nvim_proc;
    uv_pipe_t m_in_pipe;
    uv_pipe_t m_out_pipe;
    std::string m_nvim_exe;
    std::string m_nvim_cwd;
    uint64_t m_nvim_channel{0};
    uint64_t m_nvim_api_compatible{0};
    uint64_t m_nvim_api_level{0};
    std::vector<std::string> m_nvim_ui_options;

    /* msgpack-related */
    std::atomic<uint32_t> m_nvim_msgid{1};
    std::vector<char> m_nvim_resp_buf;
    std::unordered_map<uint32_t, std::shared_ptr<NvimRequest>> m_requests;
};

class NvimRequest {
  public:
    explicit NvimRequest(
        uint32_t msgid, const std::string& method, uint8_t param_count,
        std::weak_ptr<NvimWidget> nvim,
        std::function<void(msgpack::object&)>&& on_result,
        std::function<void(int32_t, const std::string&)>&& on_error);

    const uint32_t msgid;

    void arg_uint8(uint8_t d);
    void arg_uint16(uint16_t d);
    void arg_uint32(uint32_t d);
    void arg_uint64(uint64_t d);
    void arg_int8(int8_t d);
    void arg_int16(int16_t d);
    void arg_int32(int32_t d);
    void arg_int64(int64_t d);

    void arg_fix_uint8(uint8_t d);
    void arg_fix_uint16(uint16_t d);
    void arg_fix_uint32(uint32_t d);
    void arg_fix_uint64(uint64_t d);
    void arg_fix_int8(int8_t d);
    void arg_fix_int16(int16_t d);
    void arg_fix_int32(int32_t d);
    void arg_fix_int64(int64_t d);

    void arg_char(char d);
    void arg_signed_char(signed char d);
    void arg_short(int16_t d);
    void arg_int(int d);
    void arg_long(int32_t d);
    void arg_long_long(int64_t d);
    void arg_unsigned_char(uint8_t d);
    void arg_unsigned_short(uint16_t d);
    void arg_unsigned_int(uint32_t d);
    void arg_unsigned_long(uint32_t d);
    void arg_unsigned_long_long(uint64_t d);

    void arg_float(float d);
    void arg_double(double d);

    void arg_nil();
    void arg_true();
    void arg_false();

    void arg_array(size_t n);

    void arg_map(size_t n);

    void arg_str(size_t l);
    void arg_str_body(const char* b, size_t l);

    void arg_bin(size_t l);
    void arg_bin_body(const char* b, size_t l);

    void arg_ext(size_t l, int8_t type);
    void arg_ext_body(const char* b, size_t l);

  private:
    friend class NvimWidget;
    void _send();

    std::weak_ptr<NvimWidget> m_nvim;
    const std::string m_method;
    const uint8_t m_param_count;
    uint8_t m_arg_count = 0;
    std::stringstream m_buffer;
    std::unique_ptr<msgpack::packer<std::stringstream>> m_packer = nullptr;
    std::function<void(msgpack::object&)> m_on_result;
    std::function<void(int32_t, const std::string&)> m_on_error;

    std::stack<size_t> m_container_stack;
};
} // namespace ImNeovim
