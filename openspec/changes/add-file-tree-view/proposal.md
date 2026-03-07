## Why

ImNeovim currently lacks a visual file tree view, making it difficult for users to navigate and open files directly from the UI. Users must rely on Neovim's built-in file navigation commands, which can be less intuitive for those accustomed to graphical IDEs. Adding a file tree view improves usability by providing a familiar way to browse and open files.

## What Changes

- Add a new `FileTreeWidget` class that displays the current directory contents
- Create a floating, movable, and resizable window for the file tree
- Implement click-to-open functionality for files (opens in Neovim)
- Implement click-to-expand/collapse functionality for folders
- Add real-time file system watching to auto-refresh the file tree
- Integrate the file tree widget into the main application layer

## Capabilities

### New Capabilities

- `file-tree-view`: Displays current directory contents as an expandable tree, with click-to-open and auto-refresh

### Modified Capabilities

- None (no existing capabilities' requirements are changing)

## Impact

- **Files Created**:
  - `src/im_neovim/include/im_neovim/gui/file_tree_widget.h`
  - `src/im_neovim/gui/file_tree_widget.cpp`
- **Files Modified**:
  - `src/im_neovim/im_neovim_app.cpp` (add file tree widget to MyLayer)
  - `include/im_app/file_system.h` (add directory traversal and watching APIs)
  - Platform-specific `win32_file_system.cpp`, `linux_file_system.cpp`, and `darwin_file_system.mm` (add file system watching implementations)
  - `CMakeLists.txt` (add new files to build system)

## Scope

- **Current Directory Only**: The file tree will only display the contents of the directory where im_neovim was launched
- **Simple List View**: No detailed file information (size, modified time) will be shown
- **Basic Interactions**: Only click-to-open for files and click-to-expand/collapse for folders
- **Floating Window**: The file tree will be a separate floating window, not integrated into the main panel
