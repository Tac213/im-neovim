#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ImApp {
class PseudoTerminal {
  public:
    virtual ~PseudoTerminal() = default;
    virtual bool launch(uint16_t row, uint16_t col) = 0;
    virtual void terminate() = 0;
    virtual bool is_valid() = 0;
    virtual size_t write(const void* buff, size_t size) = 0;
    virtual size_t read(void* buff, size_t size) = 0;
    virtual bool resize(uint16_t row, uint16_t col) = 0;

    static std::shared_ptr<PseudoTerminal> create();
};
} // namespace ImApp
