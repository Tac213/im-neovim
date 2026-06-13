#pragma once

#include "im_neovim/workspace.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <uv.h>

namespace ImNeovim {
namespace globals {
extern uv_loop_t* g_uv_loop;

extern Workspace g_workspace;

/// Files to open once the nvim API connection is established.
/// Populated by create_im_app() from command-line arguments; consumed
/// by NvimWidget when nvim_ui_attach completes.
extern std::vector<std::filesystem::path> g_pending_startup_files;

} // namespace globals
} // namespace ImNeovim
