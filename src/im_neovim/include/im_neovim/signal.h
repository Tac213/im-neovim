#pragma once
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace ImNeovim {
template <typename... Args> class Signal {
  private:
    uint64_t m_current_conn_id = 0;
    mutable std::mutex m_mtx;
    std::unordered_map<uint64_t, std::function<void(Args...)>> m_conns;

  public:
    using Slot = std::function<void(Args...)>;

    uint64_t connect(Slot&& slot) {
        if (!slot) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(m_mtx);
        uint64_t conn_id = ++m_current_conn_id;
        m_conns.emplace(conn_id, std::move(slot));
        return conn_id;
    }

    bool disconnect(uint64_t conn_id) {
        std::lock_guard<std::mutex> lock(m_mtx);
        auto it = m_conns.find(conn_id);
        if (it != m_conns.end()) {
            m_conns.erase(it);
            return true;
        }
        return false;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_conns.clear();
    }

    void emit(Args... arguments) const {
        std::lock_guard<std::mutex> lock(m_mtx);
        /*
         * `m_conns` may be modified by Callbacks.
         */
        auto copy_conns = m_conns;
        for (const auto& pair : copy_conns) {
            const auto& callback = pair.second;
            if (callback) {
                std::invoke(callback, std::forward<Args>(arguments)...);
            }
        }
    }

    void operator()(Args... arguments) const {
        emit(std::forward<Args>(arguments)...);
    }
};

} // namespace ImNeovim