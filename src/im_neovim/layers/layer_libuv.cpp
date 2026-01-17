#include "layer_libuv.h"
#include "im_neovim/logging.h"

namespace ImNeovim {
void LayerLibuv::on_attach() {
    m_loop = static_cast<uv_loop_t*>(malloc(sizeof(uv_loop_t)));
    if (uv_loop_init(m_loop) < 0) {
        LOG_CRITICAL("Failed to initialize uv loop!");
        free(m_loop);
        m_loop = nullptr;
        return;
    }
}

void LayerLibuv::on_update() {
    if (m_loop != nullptr) {
        uv_run(m_loop, UV_RUN_ONCE);
    }
}

void LayerLibuv::on_detach() {
    if (m_loop == nullptr) {
        return;
    }
    uv_loop_close(m_loop);
    free(m_loop);
}
} // namespace ImNeovim
