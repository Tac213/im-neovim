#include "win32_pty.h"

namespace ImApp {
Win32PseudoTerminal::~Win32PseudoTerminal() {
    if (m_h_pc != INVALID_HANDLE_VALUE) {
        ::ClosePseudoConsole(m_h_pc);
    }
}

bool Win32PseudoTerminal::launch(uint16_t row, uint16_t col) {
    if (is_valid()) {
        return true;
    }
    return true;
}

void Win32PseudoTerminal::terminate() {
    if (m_h_pc != INVALID_HANDLE_VALUE) {
        ::ClosePseudoConsole(m_h_pc);
    }
    m_h_pc = INVALID_HANDLE_VALUE;
}

bool Win32PseudoTerminal::is_valid() { return m_h_pc != INVALID_HANDLE_VALUE; }

size_t Win32PseudoTerminal::write(const void* buff, size_t size) {
    if (m_h_pc == INVALID_HANDLE_VALUE) {
        return 0;
    }
    return 0;
}

size_t Win32PseudoTerminal::read(void* buff, size_t size) {
    if (m_h_pc == INVALID_HANDLE_VALUE) {
        return 0;
    }
    return 0;
}

bool Win32PseudoTerminal::resize(uint16_t row, uint16_t col) {
    if (m_h_pc == INVALID_HANDLE_VALUE) {
        return false;
    }
    COORD console_size{
        .X = static_cast<SHORT>(row),
        .Y = static_cast<SHORT>(col),
    };
    return SUCCEEDED(::ResizePseudoConsole(m_h_pc, console_size));
}

std::shared_ptr<PseudoTerminal> PseudoTerminal::create() {
    return std::make_shared<Win32PseudoTerminal>();
}
} // namespace ImApp
