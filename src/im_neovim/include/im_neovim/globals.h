#pragma once

#include "im_neovim/workspace.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <uv.h>

namespace ImNeovim {
namespace globals {
extern uv_loop_t* g_uv_loop;
}

extern Workspace g_workspace;

} // namespace ImNeovim
