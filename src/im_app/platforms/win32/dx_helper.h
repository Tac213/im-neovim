#pragma once

#include <stdexcept>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ImApp {
inline std::string hresult_to_string(HRESULT hr) {
    char s_str[64] = {};
    sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
    return std::string(s_str);
}

class HrException : public std::runtime_error {
  public:
    explicit HrException(HRESULT hr)
        : std::runtime_error(hresult_to_string(hr)), m_hr(hr) {}
    HRESULT error() const { return m_hr; }

  private:
    const HRESULT m_hr;
};

#define SAFE_RELEASE(p)                                                        \
    if (p)                                                                     \
    (p)->Release()

inline void throw_if_failed(HRESULT hr) {
    if (FAILED(hr)) {
        throw HrException(hr);
    }
}
} // namespace ImApp
