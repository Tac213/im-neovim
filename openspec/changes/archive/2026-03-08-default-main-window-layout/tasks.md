## 1. Setup and Infrastructure

- [x] 1.1 Create `DockSpaceLayout` class header and source files in `src/im_neovim/gui/`
- [x] 1.2 Add new source files to `src/im_neovim/CMakeLists.txt`
- [x] 1.3 Verify ImGui docking is enabled in the build (`IMGUI_HAS_DOCK` compile flag check)

## 2. Core DockSpace Implementation

- [x] 2.1 Implement `DockSpaceLayout::Initialize()` to create full-window DockSpace with persistent ID
- [x] 2.2 Implement `DockSpaceLayout::BuildDefaultLayout()` to create the 20/80 vertical split and 70/30 horizontal split
- [x] 2.3 Implement `DockSpaceLayout::GetDockIdForZone(enum)` to return appropriate dock IDs for FileTree, Nvim, and Terminal zones
- [x] 2.4 Implement `DockSpaceLayout::Shutdown()` for proper cleanup

## 3. Widget Docking Integration

- [x] 3.1 Modify `FileTreeWidget` to render as docked window using `ImGui::Begin()` with dock ID from `DockSpaceLayout`
- [x] 3.2 Modify `NvimWidget` to render as docked window with appropriate window flags
- [x] 3.3 Modify `TerminalWidget` to render as docked window with appropriate window flags
- [x] 3.4 Ensure all docked windows use `ImGuiWindowFlags_NoCollapse` flag
- [x] 3.5 Decide and implement consistent title bar policy (with or without title bars for docked windows)

## 4. Main Window Integration

- [x] 4.1 Modify `LayerMainWindow` (or equivalent) to instantiate `DockSpaceLayout` on initialization
- [x] 4.2 Replace direct widget placement in main window with `DockSpaceLayout::Render()` call
- [x] 4.3 Ensure proper initialization order: ImGui context → DockSpaceLayout → Widgets
- [x] 4.4 Handle cleanup order on shutdown

## 5. Layout Persistence

- [x] 5.1 Verify ImGui ini file is being saved on shutdown with dock layout data
- [x] 5.2 Verify ImGui ini file is being loaded on startup and dock layout is restored
- [x] 5.3 Implement fallback to default layout when ini file is missing or corrupted
- [x] 5.4 Test that user layout changes (resizing, undocking, redocking) persist across restarts

## 6. Reset Layout Feature

- [x] 6.1 Implement `DockSpaceLayout::ResetToDefault()` method
- [x] 6.2 Add "View > Reset Layout" menu item to the main menu bar
- [x] 6.3 Implement confirmation dialog (optional but recommended) before resetting
- [x] 6.4 Ensure reset clears current dock nodes and rebuilds default layout
- [x] 6.5 Test that reset works correctly when windows are undocked/floating

## 7. Testing and Validation

- [x] 7.1 Test default layout appears correctly on first run (no ini file)
- [x] 7.2 Test that all three panels are visible with correct initial ratios (approx 20/80, 70/30)
- [x] 7.3 Test dragging dock borders to resize panels
- [x] 7.4 Test undocking a panel by dragging it out
- [x] 7.5 Test redocking a floating panel
- [x] 7.6 Test layout persistence after closing and reopening application
- [x] 7.7 Test "Reset Layout" functionality
- [x] 7.8 Test on all supported platforms (Windows, Linux, macOS if available)

## 8. Documentation

- [x] 8.1 Add class documentation for `DockSpaceLayout` in header file
- [x] 8.2 Document public API methods with parameters and return values
- [x] 8.3 Update any relevant README or developer documentation
- [x] 8.4 Add code comments explaining non-obvious ImGui docking behavior
