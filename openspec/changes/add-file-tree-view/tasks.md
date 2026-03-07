# File Tree View Tasks

## Summary

Implement a floating file tree view for ImNeovim that shows the current directory contents with auto-refresh and click-to-open functionality.

## Tasks

### 1. Create File Tree Widget

- [ ] Create `src/im_neovim/include/im_neovim/gui/file_tree_widget.h` - Header file
- [ ] Create `src/im_neovim/gui/file_tree_widget.cpp` - Implementation file
- [ ] Implement basic widget functionality:
  - Directory scanning
  - File rendering
  - Expand/collapse folders
  - Click to open files

### 2. Integrate with Application

- [ ] Modify `src/im_neovim/im_neovim_app.cpp` to add file tree widget to MyLayer
- [ ] Modify `CMakeLists.txt` to include new files in the build system

### 3. Enhance File System Utilities

- [ ] Modify `include/im_app/file_system.h` to add directory traversal APIs
- [ ] Implement platform-specific file system watching:
  - `src/im_app/platforms/win32/win32_file_system.cpp`
  - `src/im_app/platforms/linux/linux_file_system.cpp`
  - `src/im_app/platforms/darwin/darwin_file_system.mm`

### 4. Implement File System Watching

- [ ] Create cross-platform file system watcher interface
- [ ] Implement real-time directory change detection
- [ ] Auto-refresh file tree when changes occur

### 5. Test and Debug

- [ ] Test on all supported platforms (Windows, Linux, macOS)
- [ ] Verify file opening functionality
- [ ] Verify auto-refresh behavior
- [ ] Check for memory leaks and crashes

## Verification

To verify the implementation:

1. Build and run the application
2. Confirm the file tree window is visible and shows the current directory contents
3. Test clicking on files (should open in Neovim)
4. Test clicking on folders (should expand/collapse)
5. Make changes to the directory (add/remove/rename files) and verify the file tree updates automatically
6. Test resizing and moving the floating window

## Acceptance Criteria

- The file tree view is displayed as a floating window on startup
- Files and folders in the current directory are shown
- Clicking a file opens it in Neovim
- Clicking a folder expands/collapses its contents
- The file tree auto-refreshes when files/folders are created/deleted/renamed
- The window is movable and resizable
- The file tree works correctly on all supported platforms
