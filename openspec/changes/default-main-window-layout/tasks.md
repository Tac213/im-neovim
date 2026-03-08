## 1. Setup and Infrastructure

- [ ] 1.1 Create `DockSpaceLayout` class header and source files in `src/im_neovim/gui/`
- [ ] 1.2 Add new source files to `src/im_neovim/CMakeLists.txt`
- [ ] 1.3 Verify ImGui docking is enabled in the build (`IMGUI_HAS_DOCK` compile flag check)

## 2. Core DockSpace Implementation

- [ ] 2.1 Implement `DockSpaceLayout::Initialize()` to create full-window DockSpace with persistent ID
- [ ] 2.2 Implement `DockSpaceLayout::BuildDefaultLayout()` to create the 20/80 vertical split and 70/30 horizontal split
- [ ] 2.3 Implement `DockSpaceLayout::GetDockIdForZone(enum)` to return appropriate dock IDs for FileTree, Nvim, and Terminal zones
- [ ] 2.4 Implement `DockSpaceLayout::Shutdown()` for proper cleanup

## 3. Widget Docking Integration

- [ ] 3.1 Modify `FileTreeWidget` to render as docked window using `ImGui::Begin()` with dock ID from `DockSpaceLayout`
- [ ] 3.2 Modify `NvimWidget` to render as docked window with appropriate window flags
- [ ] 3.3 Modify `TerminalWidget` to render as docked window with appropriate window flags
- [ ] 3.4 Ensure all docked windows use `ImGuiWindowFlags_NoCollapse` flag
- [ ] 3.5 Decide and implement consistent title bar policy (with or without title bars for docked windows)

## 4. Main Window Integration

- [ ] 4.1 Modify `LayerMainWindow` (or equivalent) to instantiate `DockSpaceLayout` on initialization
- [ ] 4.2 Replace direct widget placement in main window with `DockSpaceLayout::Render()` call
- [ ] 4.3 Ensure proper initialization order: ImGui context → DockSpaceLayout → Widgets
- [ ] 4.4 Handle cleanup order on shutdown

## 5. Layout Persistence

- [ ] 5.1 Verify ImGui ini file is being saved on shutdown with dock layout data
- [ ] 5.2 Verify ImGui ini file is being loaded on startup and dock layout is restored
- [ ] 5.3 Implement fallback to default layout when ini file is missing or corrupted
- [ ] 5.4 Test that user layout changes (resizing, undocking, redocking) persist across restarts

## 6. Reset Layout Feature

- [ ] 6.1 Implement `DockSpaceLayout::ResetToDefault()` method
- [ ] 6.2 Add "View > Reset Layout" menu item to the main menu bar
- [ ] 6.3 Implement confirmation dialog (optional but recommended) before resetting
- [ ] 6.4 Ensure reset clears current dock nodes and rebuilds default layout
- [ ] 6.5 Test that reset works correctly when windows are undocked/floating

## 7. Testing and Validation

- [ ] 7.1 Test default layout appears correctly on first run (no ini file)
- [ ] 7.2 Test that all three panels are visible with correct initial ratios (approx 20/80, 70/30)
- [ ] 7.3 Test dragging dock borders to resize panels
- [ ] 7.4 Test undocking a panel by dragging it out
- [ ] 7.5 Test redocking a floating panel
- [ ] 7.6 Test layout persistence after closing and reopening application
- [ ] 7.7 Test "Reset Layout" functionality
- [ ] 7.8 Test on all supported platforms (Windows, Linux, macOS if available)

## 8. Documentation

- [ ] 8.1 Add class documentation for `DockSpaceLayout` in header file
- [ ] 8.2 Document public API methods with parameters and return values
- [ ] 8.3 Update any relevant README or developer documentation
- [ ] 8.4 Add code comments explaining non-obvious ImGui docking behavior
