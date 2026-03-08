## Why

ImNeovim currently lacks a consistent default window layout. Users need a structured interface that organizes the file tree, editor, and terminal in an intuitive arrangement. This change establishes a default layout that matches modern IDE conventions (sidebar + editor + terminal), improving the out-of-box experience while leveraging ImGui's native docking capabilities.

## What Changes

- **DockSpace Setup**: Create a central `ImGui::DockSpace()` in the main window to host all panel windows
- **Default Layout Configuration**: Define the initial dock layout with:
  - Left dock node (20% width): File tree widget docked as a window
  - Right dock node (80% width): Split horizontally into:
    - Top dock node (70% height): Nvim widget docked as a window
    - Bottom dock node (30% height): Terminal widget docked as a window
- **Layout Persistence**: Leverage ImGui's .ini save/load system to persist user layout changes across restarts
- **Window Docking Flags**: Configure docked windows with `ImGuiWindowFlags_NoTitleBar`, `ImGuiWindowFlags_NoMove`, and appropriate sizing flags for IDE-like behavior

## Capabilities

### New Capabilities
- `dockspace-layout-manager`: Service that initializes and manages the ImGui DockSpace, handles layout configuration, and coordinates window docking
- `layout-persistence`: Integration with ImGui's ini system for saving/restoring dock layouts

### Modified Capabilities
- *(none)* - This is a new layout system built on ImGui's native docking; existing widgets remain unchanged

## Impact

- **Source Files**: New files in `src/im_neovim/gui/` for the DockSpace layout manager
- **Main Window**: `LayerMainWindow` will create and host the central DockSpace instead of direct widget placement
- **Existing Widgets**: File tree, nvim widget, and terminal widgets will be created as dockable ImGui windows instead of direct child widgets
- **CMake**: Add new source files to `src/im_neovim/CMakeLists.txt`
- **Dependencies**: Requires ImGui built with `IMGUI_HAS_DOCK` enabled (already satisfied per user input)
- **User Experience**: Users can now drag windows between dock nodes, collapse panels, and customize the layout; changes persist automatically
