#include "im_neovim/gui/nvim_input.h"

namespace ImNeovim {

namespace {

struct KeyMapping {
    ImGuiKey imgui_key;
    std::string_view nvim_name;
};

constexpr KeyMapping g_special_keys[] = {
    {ImGuiKey_Tab, "Tab"},
    {ImGuiKey_LeftArrow, "Left"},
    {ImGuiKey_RightArrow, "Right"},
    {ImGuiKey_UpArrow, "Up"},
    {ImGuiKey_DownArrow, "Down"},
    {ImGuiKey_PageUp, "PageUp"},
    {ImGuiKey_PageDown, "PageDown"},
    {ImGuiKey_Home, "Home"},
    {ImGuiKey_End, "End"},
    {ImGuiKey_Insert, "Insert"},
    {ImGuiKey_Delete, "Del"},
    {ImGuiKey_Backspace, "BS"},
    {ImGuiKey_Space, "Space"},
    {ImGuiKey_Enter, "Enter"},
    {ImGuiKey_Escape, "Esc"},
    {ImGuiKey_Keypad0, "k0"},
    {ImGuiKey_Keypad1, "k1"},
    {ImGuiKey_Keypad2, "k2"},
    {ImGuiKey_Keypad3, "k3"},
    {ImGuiKey_Keypad4, "k4"},
    {ImGuiKey_Keypad5, "k5"},
    {ImGuiKey_Keypad6, "k6"},
    {ImGuiKey_Keypad7, "k7"},
    {ImGuiKey_Keypad8, "k8"},
    {ImGuiKey_Keypad9, "k9"},
    {ImGuiKey_KeypadDecimal, "kPoint"},
    {ImGuiKey_KeypadDivide, "kDivide"},
    {ImGuiKey_KeypadMultiply, "kMultiply"},
    {ImGuiKey_KeypadSubtract, "kMinus"},
    {ImGuiKey_KeypadAdd, "kPlus"},
    {ImGuiKey_KeypadEnter, "kEnter"},
    {ImGuiKey_KeypadEqual, "kEqual"},
    {ImGuiKey_F1, "F1"},
    {ImGuiKey_F2, "F2"},
    {ImGuiKey_F3, "F3"},
    {ImGuiKey_F4, "F4"},
    {ImGuiKey_F5, "F5"},
    {ImGuiKey_F6, "F6"},
    {ImGuiKey_F7, "F7"},
    {ImGuiKey_F8, "F8"},
    {ImGuiKey_F9, "F9"},
    {ImGuiKey_F10, "F10"},
    {ImGuiKey_F11, "F11"},
    {ImGuiKey_F12, "F12"},
    {ImGuiKey_F13, "F13"},
    {ImGuiKey_F14, "F14"},
    {ImGuiKey_F15, "F15"},
    {ImGuiKey_F16, "F16"},
    {ImGuiKey_F17, "F17"},
    {ImGuiKey_F18, "F18"},
    {ImGuiKey_F19, "F19"},
    {ImGuiKey_F20, "F20"},
    {ImGuiKey_F21, "F21"},
    {ImGuiKey_F22, "F22"},
    {ImGuiKey_F23, "F23"},
    {ImGuiKey_F24, "F24"},
};

constexpr size_t g_special_keys_count =
    sizeof(g_special_keys) / sizeof(g_special_keys[0]);

// Control characters that are handled as special keys, not text.
// These come through io.InputQueueCharacters on some backends and should
// be skipped in the text-input pass since they're already handled as
// ImGuiKey events.
bool is_handled_control_char(ImWchar c) {
    switch (c) {
    case '\r': // Enter
    case '\n': // Enter (alternative)
    case '\t': // Tab
    case '\b': // Backspace
    case 0x7F: // Delete
    case 0x1B: // Escape
    case ' ':  // Space — handled as <Space> special key in Pass 1
        return true;
    default:
        return false;
    }
}

} // namespace

std::string prefix_without_modifier(const std::string& prefix,
                                    char mod) noexcept {
    std::string result = prefix;
    std::string target(1, mod);
    target += '-';
    auto pos = result.find(target);
    if (pos != std::string::npos) {
        result.erase(pos, 2);
    }
    return result;
}

#if !defined(IM_APP_WIN32)
static void adjust_mods_for_layout_alt(const ImGuiIO& io, std::string& prefix,
                                       ImWchar c) noexcept {
    if (prefix.find("A-") == std::string::npos) {
        return;
    }
    // High-ASCII with Alt: Alt was the input method (e.g. Option+E on macOS).
    if (c >= 0x80) {
        prefix = prefix_without_modifier(prefix, 'A');
        return;
    }
    // Low-ASCII chars that require Alt on non-US layouts.
    // Match neovim-qt input_mac.cpp IsAsciiCharRequiringAlt().
    ImGuiKey expected = ImGuiKey_None;
    switch (c) {
    case '[':
        expected = ImGuiKey_LeftBracket;
        break;
    case ']':
        expected = ImGuiKey_RightBracket;
        break;
    case '{':
        expected = ImGuiKey_LeftBracket;
        break;
    case '}':
        expected = ImGuiKey_RightBracket;
        break;
    case '|':
        expected = ImGuiKey_Backslash;
        break;
    case '~':
        expected = ImGuiKey_GraveAccent;
        break;
    case '@':
        expected = ImGuiKey_2;
        break;
    default:
        return;
    }
    if (!ImGui::IsKeyDown(expected)) {
        prefix = prefix_without_modifier(prefix, 'A');
    }
}
#else
static void adjust_mods_for_layout_alt(const ImGuiIO&, std::string&,
                                       ImWchar) noexcept {}
#endif

std::string_view special_key_name(ImGuiKey key) noexcept {
    for (size_t i = 0; i < g_special_keys_count; i++) {
        if (g_special_keys[i].imgui_key == key) {
            return g_special_keys[i].nvim_name;
        }
    }
    return {};
}

std::string modifier_prefix(const ImGuiIO& io) noexcept {
    std::string prefix;
#if defined(IM_APP_DARWIN)
    // macOS: physical Ctrl is io.KeySuper, Cmd is io.KeyCtrl
    // Per neovim-qt input_mac.cpp: Cmd(KeyCtrl)->D-, Ctrl(KeySuper)->C-
    if (io.KeyCtrl) {
        prefix += "D-";
    }
    if (io.KeySuper) {
        prefix += "C-";
    }
#elif defined(IM_APP_WIN32)
    // Windows: never pass C- and A- together — Ctrl+Alt is AltGr.
    // Matching neovim-qt input_win32.cpp GetModifierPrefix().
    if (io.KeyCtrl && !io.KeyAlt) {
        prefix += "C-";
    }
#else
    // Linux: Ctrl->C-
    if (io.KeyCtrl) {
        prefix += "C-";
    }
#endif
    if (io.KeyShift) {
        prefix += "S-";
    }
#if defined(IM_APP_WIN32)
    if (io.KeyAlt && !io.KeyCtrl) {
        prefix += "A-";
    }
#else
    if (io.KeyAlt) {
        prefix += "A-";
    }
#endif
    return prefix;
}

std::string collect_input(const ImGuiIO& io, ImGuiKey consumed_key) noexcept {
    std::string result;

    // Track which ImGuiKeys we've already handled so we don't double-send
    // a key that appears both as an ImGuiKey event and in InputQueueCharacters.
    bool key_handled[ImGuiKey_NamedKey_COUNT] = {};
    if (consumed_key != ImGuiKey_None) {
        int idx = consumed_key - ImGuiKey_NamedKey_BEGIN;
        if (idx >= 0 && idx < ImGuiKey_NamedKey_COUNT) {
            key_handled[idx] = true;
        }
    }

    std::string mods = modifier_prefix(io);

    // Ctrl+^ (Ctrl+6): toggle-input-method key.
    // ImGuiKey_6 is not in s_special_keys; handle it here so Pass 2 can
    // skip any corresponding character. Matching neovim-qt input.cpp.
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_6, false)) {
        result += "<C-^>";
        int idx_6 = ImGuiKey_6 - ImGuiKey_NamedKey_BEGIN;
        if (idx_6 >= 0 && idx_6 < ImGuiKey_NamedKey_COUNT) {
            key_handled[idx_6] = true;
        }
    }

    // Pass 1: Special keys via ImGui::IsKeyPressed
    for (size_t i = 0; i < g_special_keys_count; i++) {
        ImGuiKey key = g_special_keys[i].imgui_key;
        if (ImGui::IsKeyPressed(key, false)) {
            int idx = key - ImGuiKey_NamedKey_BEGIN;
            if (idx >= 0 && idx < ImGuiKey_NamedKey_COUNT && key_handled[idx]) {
                continue;
            }
            result += '<';
            result += mods;
            result += g_special_keys[i].nvim_name;
            result += '>';
            if (idx >= 0 && idx < ImGuiKey_NamedKey_COUNT) {
                key_handled[idx] = true;
            }
        }
    }

    // Pass 2: Text input from io.InputQueueCharacters
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        ImWchar c = io.InputQueueCharacters[i];

        // Skip whitespace-like control characters handled in Pass 1
        if (is_handled_control_char(c)) {
            continue;
        }

        // Ctrl+^ arriving as 0x1E (RS) or '^' with Ctrl held
        if (c == 0x1E || (c == '^' && io.KeyCtrl)) {
            int idx_6 = ImGuiKey_6 - ImGuiKey_NamedKey_BEGIN;
            if (idx_6 < 0 || idx_6 >= ImGuiKey_NamedKey_COUNT ||
                !key_handled[idx_6]) {
                result += "<C-^>";
                if (idx_6 >= 0 && idx_6 < ImGuiKey_NamedKey_COUNT) {
                    key_handled[idx_6] = true;
                }
            }
            continue;
        }

        // Escape '<' as <lt> — '<' is special in Neovim key notation.
        // Strip Shift from prefix since '<' implies Shift on US keyboards.
        if (c == '<') {
            result += '<';
            result += prefix_without_modifier(mods, 'S');
            result += "lt>";
            continue;
        }

        // Escape '\' as <Bslash>
        if (c == '\\') {
            result += '<';
            result += mods;
            result += "Bslash>";
            continue;
        }

        if (c >= 1 && c <= 26) {
            // Ctrl+A through Ctrl+Z — the control char takes priority
            // over the ImGuiKey since it carries the Ctrl info.
            result += "<C-";
            result += static_cast<char>('a' + c - 1);
            result += '>';
            // Mark the corresponding letter key as handled
            int letter_idx = ImGuiKey_A + (c - 1) - ImGuiKey_NamedKey_BEGIN;
            if (letter_idx >= 0 && letter_idx < ImGuiKey_NamedKey_COUNT) {
                key_handled[letter_idx] = true;
            }
        } else if (c == 0) {
            // NUL: Ctrl+Space on some platforms
            result += "<C-Space>";
        } else if (c >= 0x20) {
            // NumLock dedup: when NumLock is ON, keypad keys produce
            // both a Pass 1 ImGuiKey event AND the character in
            // InputQueueCharacters. Skip the character when the
            // corresponding keypad key already fired in Pass 1.
            ImGuiKey kp = ImGuiKey_None;
            if (c >= '0' && c <= '9') {
                kp = static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (c - '0'));
            } else if (c == '.') {
                kp = ImGuiKey_KeypadDecimal;
            } else if (c == '/') {
                kp = ImGuiKey_KeypadDivide;
            } else if (c == '*') {
                kp = ImGuiKey_KeypadMultiply;
            } else if (c == '-') {
                kp = ImGuiKey_KeypadSubtract;
            } else if (c == '+') {
                kp = ImGuiKey_KeypadAdd;
            } else if (c == '=') {
                kp = ImGuiKey_KeypadEqual;
            }
            if (kp != ImGuiKey_None && ImGui::IsKeyPressed(kp, false)) {
                int kp_idx = kp - ImGuiKey_NamedKey_BEGIN;
                if (kp_idx >= 0 && kp_idx < ImGuiKey_NamedKey_COUNT) {
                    key_handled[kp_idx] = true;
                }
                continue;
            }
            // Printable character — encode as UTF-8 (ImWchar is 16-bit)
            if (c < 0x80) {
                result += static_cast<char>(c);
            } else if (c < 0x800) {
                result += static_cast<char>(0xC0 | (c >> 6));
                result += static_cast<char>(0x80 | (c & 0x3F));
            } else {
                result += static_cast<char>(0xE0 | (c >> 12));
                result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (c & 0x3F));
            }
            // Mark letter keys as handled so Pass 3 doesn't double-emit
            char lower = (c >= 'A' && c <= 'Z')   ? static_cast<char>(c + 32)
                         : (c >= 'a' && c <= 'z') ? static_cast<char>(c)
                                                  : 0;
            if (lower >= 'a' && lower <= 'z') {
                int letter_idx =
                    ImGuiKey_A + (lower - 'a') - ImGuiKey_NamedKey_BEGIN;
                if (letter_idx >= 0 && letter_idx < ImGuiKey_NamedKey_COUNT) {
                    key_handled[letter_idx] = true;
                }
            }
        }
    }

    // Pass 3: Ctrl/Meta/Alt+letter that didn't produce a control char or
    // come through InputQueueCharacters. On some backends (non-Windows),
    // Ctrl+letter only shows up as IsKeyPressed on the letter key with the
    // modifier set, but without a corresponding entry in InputQueueCharacters.
    // Skip when only Shift is held — the shifted character already arrived
    // via InputQueueCharacters.
#if defined(IM_APP_DARWIN)
    bool has_non_shift_mod = io.KeySuper || io.KeyCtrl || io.KeyAlt;
#else
    bool has_non_shift_mod = io.KeyCtrl || io.KeyAlt;
#endif
    if (has_non_shift_mod) {
        for (int i = 0; i < 26; i++) {
            ImGuiKey key = static_cast<ImGuiKey>(ImGuiKey_A + i);
            int idx = key - ImGuiKey_NamedKey_BEGIN;
            if (idx >= 0 && idx < ImGuiKey_NamedKey_COUNT &&
                !key_handled[idx] && ImGui::IsKeyPressed(key, false)) {
                result += '<';
                result += mods;
                result += static_cast<char>('a' + i);
                result += '>';
            }
        }
    }

    return result;
}

std::string mouse_button_name(ImGuiMouseButton btn,
                              uint8_t click_count) noexcept {
    switch (btn) {
    case ImGuiMouseButton_Left:
        if (click_count >= 2 && click_count <= 4) {
            return std::to_string(click_count) + "-Left";
        }
        return "Left";
    case ImGuiMouseButton_Right:
        return "Right";
    case ImGuiMouseButton_Middle:
        return "Middle";
    case 3: // ImGuiMouseButton_X1
        return "X1";
    case 4: // ImGuiMouseButton_X2
        return "X2";
    default:
        return {};
    }
}

std::string mouse_event_suffix(bool is_release, bool is_drag) noexcept {
    if (is_release) {
        return "Release";
    }
    if (is_drag) {
        return "Drag";
    }
    return "Mouse";
}

std::string mouse_input_string(const std::string& mods, std::string_view button,
                               std::string_view event, int col,
                               int row) noexcept {
    std::string result;
    result += '<';
    result += mods;
    result += button;
    result += event;
    result += "><";
    result += std::to_string(col);
    result += ',';
    result += std::to_string(row);
    result += '>';
    return result;
}

std::string convert_scroll(float delta_y, float delta_x,
                           const std::string& mods, int col, int row,
                           float& rem_y, float& rem_x) noexcept {
    std::string result;
    std::string coord = std::to_string(col) + ',' + std::to_string(row);

    float acc_y = delta_y + rem_y;
    int steps_y = static_cast<int>(acc_y);
    rem_y = acc_y - static_cast<float>(steps_y);

    while (steps_y > 0) {
        result += '<';
        result += mods;
        result += "ScrollWheelUp><";
        result += coord;
        result += '>';
        steps_y--;
    }
    while (steps_y < 0) {
        result += '<';
        result += mods;
        result += "ScrollWheelDown><";
        result += coord;
        result += '>';
        steps_y++;
    }

    float acc_x = delta_x + rem_x;
    int steps_x = static_cast<int>(acc_x);
    rem_x = acc_x - static_cast<float>(steps_x);

    while (steps_x > 0) {
        result += '<';
        result += mods;
        result += "ScrollWheelRight><";
        result += coord;
        result += '>';
        steps_x--;
    }
    while (steps_x < 0) {
        result += '<';
        result += mods;
        result += "ScrollWheelLeft><";
        result += coord;
        result += '>';
        steps_x++;
    }

    return result;
}

} // namespace ImNeovim
