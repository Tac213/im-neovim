#pragma once

#include <cstdint>
#include <imgui.h>
#include <string>
#include <string_view>

namespace ImNeovim {

// Map an ImGuiKey to its Neovim special-key name (e.g. ImGuiKey_UpArrow ->
// "Up"). Returns empty string_view for non-special keys (letters, digits,
// punctuation).
std::string_view special_key_name(ImGuiKey key) noexcept;

// Build the modifier prefix from ImGuiIO state (e.g. "C-", "C-S-", "A-").
// Platform-specific mapping, matching neovim-qt conventions:
//   macOS:   KeySuper -> C- (physical Ctrl), KeyCtrl -> D- (Cmd)
//   Windows: KeyCtrl  -> C-, KeyShift -> S-, KeyAlt -> A-
//   Linux:   KeyCtrl  -> C-, KeyShift -> S-, KeyAlt -> A-
std::string modifier_prefix(const ImGuiIO& io) noexcept;

// Remove a single modifier character from a modifier-prefix string.
// E.g., prefix_without_modifier("C-S-", 'S') returns "C-".
// Returns the modified string; does not modify the input.
std::string prefix_without_modifier(const std::string& prefix,
                                    char mod) noexcept;

// Collect all pending keyboard input from ImGuiIO into a single Neovim
// key-notation string. Handles special keys (<Up>, <C-F1>, etc.), text
// characters (printable & UTF-8), and control characters (Ctrl+A..Z).
// If consumed_key != ImGuiKey_None, that key is pre-marked as handled
// (e.g. to suppress a paste shortcut that was already processed).
// The returned string is ready to pass to nvim_input().
std::string collect_input(const ImGuiIO& io,
                          ImGuiKey consumed_key = ImGuiKey_None) noexcept;

// --- Mouse input conversion ---

// Get the Neovim button name for an ImGui mouse button.
// click_count 2-4 produces "N-Left"; 0/1 produces just "Left".
std::string mouse_button_name(ImGuiMouseButton btn,
                              uint8_t click_count) noexcept;

// Get the Neovim event suffix for a mouse action:
// "Mouse" (press), "Release", or "Drag".
std::string mouse_event_suffix(bool is_release, bool is_drag) noexcept;

// Build a complete mouse notation string in the format
// <[mods]ButtonEvent><col,row>.
std::string mouse_input_string(const std::string& mods, std::string_view button,
                               std::string_view event, int col,
                               int row) noexcept;

// Convert scroll deltas to zero or more mouse scroll notation strings
// with remainder accumulation for smooth trackpad scrolling.
// Positive vertical delta  -> ScrollWheelUp
// Negative vertical delta  -> ScrollWheelDown
// Positive horizontal delta -> ScrollWheelRight
// Negative horizontal delta -> ScrollWheelLeft
std::string convert_scroll(float delta_y, float delta_x,
                           const std::string& mods, int col, int row,
                           float& rem_y, float& rem_x) noexcept;

} // namespace ImNeovim
