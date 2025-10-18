#pragma once

#include "im_app/pty.h"
// Block minwindef.h min/max macros to prevent <algorithm> conflict
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOMCX
#define NOHELP
#define NOCOMM
#include <Windows.h>

namespace ImApp {
class Win32PseudoTerminal : public PseudoTerminal {
  public:
    virtual ~Win32PseudoTerminal();
    virtual bool launch(uint16_t row, uint16_t col) override;
    virtual void terminate() override;
    virtual bool is_valid() override;
    virtual size_t write(const void* buff, size_t size) override;
    virtual size_t read(void* buff, size_t size) override;
    virtual bool resize(uint16_t row, uint16_t col) override;

  private:
    HPCON m_h_pc{INVALID_HANDLE_VALUE};
};
} // namespace ImApp
