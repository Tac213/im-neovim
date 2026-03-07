# File Tree View Spec

## Summary

A floating window that displays the contents of the current directory as an expandable tree.

## ADDED Requirements

### Requirement: Show files and folders in current directory

The file tree SHALL show files and folders in the current working directory.

#### Scenario: Display directory contents

Given the file tree view is open
When the user looks at the window
Then all files and folders in the current working directory are displayed

### Requirement: Sort entries

The file tree SHALL sort entries: directories first (alphabetically), then files (alphabetically).

#### Scenario: Sorted directory listing

Given the file tree view is open
When the entries are rendered
Then directories are listed first (alphabetically) followed by files (alphabetically)

### Requirement: Auto-refresh on file system changes

The file tree SHALL auto-refresh when file system changes occur.

#### Scenario: File tree updates when directory changes

Given the file tree view is open
When a file or folder is created, deleted, or renamed
Then the file tree automatically refreshes to show the changes

### Requirement: Click to open file

The file tree SHALL allow clicking on a file to open it in Neovim.

#### Scenario: Opening a file

Given the file tree view is open
When the user clicks on a file
Then the file is opened in Neovim

### Requirement: Click to expand/collapse folder

The file tree SHALL allow clicking on a folder to toggle expand/collapse.

#### Scenario: Expanding and collapsing folders

Given the file tree view is open
When the user clicks on a folder
Then the folder's contents are expanded or collapsed

### Requirement: Resize and move window

The file tree SHALL allow resizing and moving the floating window.

#### Scenario: Resizing the window

Given the file tree view is open
When the user resizes the window
Then the window responds appropriately

#### Scenario: Moving the window

Given the file tree view is open
When the user moves the window
Then the window is repositioned

### Requirement: Floating window with title bar

The file tree SHALL be a floating, resizable window with title bar.

#### Scenario: Window appearance

Given the file tree view is open
When the user looks at the window
Then it is a floating window with a title bar

### Requirement: Tree node indicators

The file tree SHALL use ImGui's builtin tree node expand/collapse indicators.

#### Scenario: Folder indicators

Given the file tree view is open
When a folder is displayed
Then it shows ImGui's builtin tree node expand/collapse indicator

### Requirement: Handle permission errors

The file tree SHALL handle permission errors when accessing files/folders.

#### Scenario: Permission denied

Given the file tree view is open
When the user tries to access a file/folder they don't have permission to
Then an appropriate error message is shown

### Requirement: Directory becomes inaccessible

The file tree SHALL continue to function if directory becomes inaccessible.

#### Scenario: Directory deletion

Given the file tree view is open and showing a directory
When the directory becomes inaccessible (e.g., it's deleted)
Then the file tree continues to function and shows an error message

## API

### FileTreeWidget Class

```cpp
class FileTreeWidget {
public:
    FileTreeWidget();
    ~FileTreeWidget();

    void render();

    const std::string& window_title() const;
    void set_window_title(const std::string& title);
    bool is_visible() const;
    void set_visible(bool visible);
};
```

### Integration with NeovimWidget

```cpp
void NvimWidget::open_file(const std::string& path);
```

## Platform Support

- **Windows**: Full support
- **Linux**: Full support
- **macOS**: Full support
