#include "im_neovim/globals.h"

namespace ImNeovim {
namespace globals {
uv_loop_t* g_uv_loop = nullptr;
}

Workspace g_workspace;

std::vector<std::filesystem::path> g_pending_startup_files;

} // namespace ImNeovim
