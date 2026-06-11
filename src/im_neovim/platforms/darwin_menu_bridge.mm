#include "im_neovim/platforms/darwin_menu_bridge.h"

#import <Cocoa/Cocoa.h>

namespace ImNeovim {

// -- Global signal definitions --
Signal<> g_native_on_new_window;
Signal<> g_native_on_open_folder;
Signal<> g_native_on_add_folder_to_workspace;
Signal<> g_native_on_about;
Signal<> g_native_on_reset_layout;
Signal<> g_native_on_exit;
Signal<> g_native_on_toggle_file_tree;
Signal<> g_native_on_toggle_terminal;

} // namespace ImNeovim

// -- Private ObjC helper: receives menu actions and forwards to signals --

@interface ImNeovimMenuTarget : NSObject
@end

@implementation ImNeovimMenuTarget

- (void)newWindowAction:(id)sender {
    (void)sender;
    ImNeovim::g_native_on_new_window.emit();
}

- (void)openFolderAction:(id)sender {
    (void)sender;
    ImNeovim::g_native_on_open_folder.emit();
}

- (void)addFolderToWorkspaceAction:(id)sender {
    (void)sender;
    ImNeovim::g_native_on_add_folder_to_workspace.emit();
}

- (void)aboutAction:(id)sender {
    (void)sender;
    ImNeovim::g_native_on_about.emit();
}

- (void)resetLayoutAction:(id)sender {
    (void)sender;
    ImNeovim::g_native_on_reset_layout.emit();
}

- (void)closeWindowAction:(id)sender {
    (void)sender;
    ImNeovim::g_native_on_exit.emit();
}

- (void)toggleFileTreeAction:(id)sender {
    (void)sender;
    ImNeovim::g_native_on_toggle_file_tree.emit();
}

- (void)toggleTerminalAction:(id)sender {
    (void)sender;
    ImNeovim::g_native_on_toggle_terminal.emit();
}

@end

// -- File-level references to menu items that need state updates --
static NSMenuItem *s_file_tree_menu_item = nil;
static NSMenuItem *s_terminal_menu_item = nil;

// -- Public setup function --

namespace ImNeovim {

void darwin_setup_native_menus() {
    // Single instance that lives for the application lifetime
    static ImNeovimMenuTarget *s_target = [[ImNeovimMenuTarget alloc] init];

    NSMenu *main_menu = [NSApp mainMenu];
    if (main_menu == nil) {
        return;
    }

    // ---- App menu: replace About action ----
    // GLFW creates the app menu as the first item (title = "").
    if ([main_menu numberOfItems] > 0) {
        NSMenuItem *app_menu_item = [main_menu itemAtIndex:0];
        NSMenu *app_menu = [app_menu_item submenu];
        if (app_menu != nil) {
            for (NSMenuItem *item in [app_menu itemArray]) {
                if ([item action] == @selector(orderFrontStandardAboutPanel:)) {
                    [item setAction:@selector(aboutAction:)];
                    [item setTarget:s_target];
                    break;
                }
            }
        }
    }

    // ---- File menu ----
    NSMenu *file_menu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *file_menu_item = [[NSMenuItem alloc] initWithTitle:@"File"
                                                            action:nil
                                                     keyEquivalent:@""];
    [file_menu_item setSubmenu:file_menu];

    NSMenuItem *new_window_item =
        [file_menu addItemWithTitle:@"New Window"
                             action:@selector(newWindowAction:)
                      keyEquivalent:@"N"];
    [new_window_item setKeyEquivalentModifierMask:NSEventModifierFlagShift |
                                                  NSEventModifierFlagCommand];
    [new_window_item setTarget:s_target];

    [file_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *open_item =
        [file_menu addItemWithTitle:@"Open Folder..."
                             action:@selector(openFolderAction:)
                      keyEquivalent:@"o"];
    [open_item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [open_item setTarget:s_target];

    NSMenuItem *add_item =
        [file_menu addItemWithTitle:@"Add Folder to Workspace..."
                             action:@selector(addFolderToWorkspaceAction:)
                      keyEquivalent:@""];
    [add_item setTarget:s_target];

    [file_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *close_item =
        [file_menu addItemWithTitle:@"Close Window"
                             action:@selector(closeWindowAction:)
                      keyEquivalent:@"w"];
    [close_item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [close_item setTarget:s_target];

    // Insert after the app menu (index 1)
    [main_menu insertItem:file_menu_item atIndex:1];

    // ---- View menu ----
    NSMenu *view_menu = [[NSMenu alloc] initWithTitle:@"View"];
    NSMenuItem *view_menu_item = [[NSMenuItem alloc] initWithTitle:@"View"
                                                            action:nil
                                                     keyEquivalent:@""];
    [view_menu_item setSubmenu:view_menu];

    NSMenuItem *reset_item =
        [view_menu addItemWithTitle:@"Reset Layout"
                             action:@selector(resetLayoutAction:)
                      keyEquivalent:@""];
    [reset_item setTarget:s_target];

    [view_menu addItem:[NSMenuItem separatorItem]];

    s_file_tree_menu_item =
        [view_menu addItemWithTitle:@"File Tree"
                             action:@selector(toggleFileTreeAction:)
                      keyEquivalent:@"E"];
    [s_file_tree_menu_item
        setKeyEquivalentModifierMask:NSEventModifierFlagCommand |
                                     NSEventModifierFlagShift];
    [s_file_tree_menu_item setTarget:s_target];
    [s_file_tree_menu_item setState:NSControlStateValueOff];

    s_terminal_menu_item =
        [view_menu addItemWithTitle:@"Terminal"
                             action:@selector(toggleTerminalAction:)
                      keyEquivalent:@"`"];
    [s_terminal_menu_item
        setKeyEquivalentModifierMask:NSEventModifierFlagControl];
    [s_terminal_menu_item setTarget:s_target];
    [s_terminal_menu_item setState:NSControlStateValueOff];

    // Insert after File (index 2)
    [main_menu insertItem:view_menu_item atIndex:2];
}

void darwin_update_file_tree_menu_state(bool visible) {
    if (s_file_tree_menu_item) {
        s_file_tree_menu_item.state =
            visible ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

void darwin_update_terminal_menu_state(bool visible) {
    if (s_terminal_menu_item) {
        s_terminal_menu_item.state =
            visible ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

} // namespace ImNeovim
