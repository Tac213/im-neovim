#include "im_neovim/platforms/darwin_menu_bridge.h"

#import <Cocoa/Cocoa.h>

namespace ImNeovim {

// -- Global signal definitions --
Signal<> g_native_on_open_folder;
Signal<> g_native_on_add_folder_to_workspace;
Signal<> g_native_on_about;
Signal<> g_native_on_reset_layout;
Signal<> g_native_on_exit;

} // namespace ImNeovim

// -- Private ObjC helper: receives menu actions and forwards to signals --

@interface ImNeovimMenuTarget : NSObject
@end

@implementation ImNeovimMenuTarget

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

@end

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

    // Insert after File (index 2)
    [main_menu insertItem:view_menu_item atIndex:2];
}

} // namespace ImNeovim
