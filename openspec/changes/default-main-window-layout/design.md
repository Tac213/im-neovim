## Context

ImNeovim currently creates and manages its widgets (file tree, nvim, terminal) directly within the main window layer. There is no centralized layout management, and widgets are positioned manually or embedded directly without a flexible docking system.

This change introduces ImGui's native docking system (enabled via `IMGUI_HAS_DOCK`) to provide a modern, user-configurable IDE-like layout. The approach leverages ImGui's built-in DockSpace and window docking capabilities.

**Key Constraints:**
- Must use ImGui's docking feature (already enabled in build)
- Must work with existing widgets (file tree, nvim, terminal)
- Must be cross-platform (Windows, Linux, macOS)
- Should persist user layout changes across restarts via ImGui's ini system

## Goals / Non-Goals

**Goals:**
- Create a central `ImGui::DockSpace` in the main window
- Define a default dock layout with 3 zones: left (file tree), top-right (nvim), bottom-right (terminal)
- Convert existing widgets to dockable ImGui windows
- Enable user customization through ImGui's native drag-and-drop docking
- Persist layout changes via ImGui's built-in ini save/load

**Non-Goals:**
- Custom splitter implementation (using ImGui's native docking)
- Manual layout persistence system (using ImGui's ini system)
- Complex layout templates/tabs (initial version focuses on the 3-panel default)
- Animated layout transitions

## Decisions

**1. Use ImGui DockSpace over manual layout management**
- **Rationale**: ImGui's docking system provides drag-and-drop, persistence, and professional IDE-like behavior out of the box
- **Alternatives considered**: Manual splitter widgets (more code, less user flexibility), custom layout engine (overkill for current needs)

**2. DockSpace as central node with docked windows**
- **Rationale**: The main window hosts a full-size DockSpace. Each widget (file tree, nvim, terminal) is created as a dockable window that docks into the layout
- **Layout structure**:
  ```
  DockSpace (root)
  ├── DockNode (Left, 20%) → FileTreeWindow
  └── DockNode (Right, 80%)
      ├── DockNode (Top, 70%) → NvimWindow
      └── DockNode (Bottom, 30%) → TerminalWindow
  ```

**3. Leverage ImGui's ini persistence**
- **Rationale**: ImGui automatically saves dock layouts to the ini file. No custom persistence code needed
- **Implementation**: Ensure `ImGui::LoadIniSettingsFromDisk` and `SaveIniSettingsToDisk` are called appropriately, or let ImGui handle it via `ImGuiConfigFlags_NavEnableGamepad` / `ImGui::GetIO().IniFilename`

**4. Window flags for IDE-like behavior**
- Each docked window will use:
  - `ImGuiWindowFlags_NoTitleBar` (optional - clean look, or keep for window identification)
  - `ImGuiWindowFlags_NoCollapse`
  - `ImGuiWindowFlags_NoNavFocus` (prevent docking operations from stealing focus)
  - `ImGuiWindowFlags_NoMove` when docked (handled by docking system)

**5. Widget integration approach**
- FileTree, NvimWidget, Terminal will each get a `Render()` or `Draw()` method if not already present
- The main loop calls these within `ImGui::Begin("WindowName")` / `End()` blocks
- Each widget manages its own internal ImGui rendering

## Risks / Trade-offs

**[Risk] ImGui docking limitations** → **Mitigation**: The default layout uses simple splits which are well-supported. Complex nested docking edge cases (e.g., user undocking all windows) are handled by ImGui's fallback behavior.

**[Risk] Window focus/keyboard handling between docked nvim and terminal** → **Mitigation**: The nvim widget already manages focus for Neovim integration. Docking doesn't change widget internals - it only changes the container. Ensure `ImGuiWindowFlags_NoNavInputs` or appropriate focus flags are set.

**[Risk] Ini file bloat or corruption affecting layout** → **Mitigation**: ImGui's ini format is robust. Users can delete the ini to reset to defaults. Optionally, we could add a "Reset Layout" menu item to programmatically rebuild the dock layout.

**[Risk] Performance overhead of docking** → **Mitigation**: ImGui docking is lightweight - it's essentially window position management. The actual widget rendering (nvim, terminal) dominates frame time, not the docking container.

**[Trade-off] Loss of pixel-perfect layout control** → **Mitigation**: Users can still resize to exact preferences; default ratios are approximate starting points. The trade-off is worth the gain in user flexibility.

## Migration Plan

**Phase 1: Core DockSpace Infrastructure**
1. Add `DockSpaceLayout` class to manage the root dockspace and dock node configuration
2. Implement default dock node layout setup (20/80 split, then 70/30 split)
3. Test with placeholder ImGui windows

**Phase 2: Widget Integration**
1. Modify existing widgets (FileTree, NvimWidget, Terminal) to render within docked windows
2. Ensure proper window flags and behavior
3. Wire widgets into the DockSpace layout

**Phase 3: Integration & Polish**
1. Replace direct widget placement in main window with DockSpace setup
2. Test layout persistence via ImGui ini
3. Add "Reset Layout" menu item (optional)

**Rollback Strategy**: Revert to previous widget placement approach. The change is additive - old direct-placement code can be restored by reverting the main window changes.

## Open Questions

1. **Title bars**: Should docked windows show title bars ("File Tree", "Editor", "Terminal") or be chromeless? Title bars help identify panels but add visual noise.

2. **Tab support**: Should we enable ImGui's tab bar within dock nodes? This would allow multiple nvim instances or terminals in the same dock node.

3. **Menu integration**: Should layout reset be in a "View" menu? What other layout commands (toggle panel visibility, etc.) should be available?

4. **Keyboard shortcuts**: Should there be shortcuts to focus specific panels (e.g., Ctrl+1 for file tree, Ctrl+2 for editor)?
