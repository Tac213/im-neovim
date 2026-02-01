#include "layer_libuv.h"
#include "im_neovim/globals.h"
#include "im_neovim/logging.h"

namespace ImNeovim {
void LayerLibuv::on_attach() {
    globals::g_uv_loop = static_cast<uv_loop_t*>(malloc(sizeof(uv_loop_t)));
    if (uv_loop_init(globals::g_uv_loop) < 0) {
        LOG_CRITICAL("Failed to initialize uv loop!");
        free(globals::g_uv_loop);
        globals::g_uv_loop = nullptr;
        return;
    }
}

void LayerLibuv::on_update() {
    if (globals::g_uv_loop != nullptr) {
        uv_run(globals::g_uv_loop, UV_RUN_NOWAIT);
    }
}

LayerLibuv::~LayerLibuv() {
    if (globals::g_uv_loop == nullptr) {
        return;
    }
    uv_loop_close(globals::g_uv_loop);
    free(globals::g_uv_loop);
    globals::g_uv_loop = nullptr;
}
} // namespace ImNeovim
