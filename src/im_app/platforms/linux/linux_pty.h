#pragma once

#include "im_app/pty.h"
#include <sys/types.h>

namespace ImApp {
class LinuxPseudoTerminal : public PseudoTerminal {
  public:
    virtual ~LinuxPseudoTerminal();
    virtual bool launch(uint16_t row, uint16_t col) override;
    virtual void terminate() override;
    virtual bool is_valid() override;
    virtual size_t write(const void* buff, size_t size) override;
    virtual size_t read(void* buff, size_t size) override;
    virtual bool resize(uint16_t row, uint16_t col) override;

  private:
    int m_pty_fd{-1};
    pid_t m_child_pid{-1};
};
} // namespace ImApp
