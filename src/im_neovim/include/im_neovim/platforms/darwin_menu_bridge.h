#pragma once

#include "im_neovim/signal.h"

namespace ImNeovim {

// Global signals for native macOS menu actions.
// Connected by LayerMainWindow::on_attach() on Darwin.

/// File > New Window (Shift+Cmd+N)
extern Signal<> g_native_on_new_window;

/// File > Open Folder... (Cmd+O)
extern Signal<> g_native_on_open_folder;

/// File > Add Folder to Workspace...
extern Signal<> g_native_on_add_folder_to_workspace;

/// App menu > About ImNeovim  (replaces orderFrontStandardAboutPanel:)
extern Signal<> g_native_on_about;

/// View > Reset Layout
extern Signal<> g_native_on_reset_layout;

/// File > Close Window (Cmd+W) / App menu > Quit
extern Signal<> g_native_on_exit;

/// View > File Tree (Cmd+Shift+E)
extern Signal<> g_native_on_toggle_file_tree;

/// View > Terminal (Ctrl+`)
extern Signal<> g_native_on_toggle_terminal;

/// Modify GLFW's default NSMenu bar to match ImNeovim's menu structure
/// and replace the standard About panel with the custom one.
/// Must be called after glfwInit() and before the main loop.
void darwin_setup_native_menus();

/// Update the checkmark state of the native File Tree menu item.
void darwin_update_file_tree_menu_state(bool visible);

/// Update the checkmark state of the native Terminal menu item.
void darwin_update_terminal_menu_state(bool visible);

} // namespace ImNeovim
