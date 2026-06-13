#include "im_neovim/gui/output_widget.h"

#include "im_app/output_capture.h"
#include <algorithm>
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace ImNeovim {

OutputWidget::OutputWidget() = default;
OutputWidget::~OutputWidget() = default;

void OutputWidget::render() {
    if (!m_is_visible) {
        return;
    }

    // Dock into the assigned dock node on first use.
    if (m_dock_id != 0) {
        ImGui::SetNextWindowDockID(m_dock_id, ImGuiCond_FirstUseEver);
    }

    // Use ImGuiWindowFlags_NoFocusOnAppearing so the output widget does
    // not steal keyboard focus from nvim when it first appears.
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoFocusOnAppearing;

    bool open = true;
    if (!ImGui::Begin(m_window_title.c_str(), &open, window_flags)) {
        ImGui::End();
        return;
    }

    // If the user closes the window via the [x] button, treat it as a
    // visibility toggle (matching how the file tree and terminal work).
    if (!open) {
        m_is_visible = false;
        ImGui::End();
        return;
    }

    _render_toolbar();
    ImGui::Separator();
    _render_log_entries();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Toolbar: filter input | Clear button | Auto-scroll checkbox
// ---------------------------------------------------------------------------

void OutputWidget::_populate_logger_names() {
    if (m_logger_names_populated) {
        return;
    }

    spdlog::apply_all([&](std::shared_ptr<spdlog::logger> logger) {
        const auto& name = logger->name();
        if (!name.empty()) {
            m_logger_names.push_back(name);
        }
    });
    std::sort(m_logger_names.begin(), m_logger_names.end());

    // Default to "ImNeoVim" if present.
    auto it =
        std::find(m_logger_names.begin(), m_logger_names.end(), "ImNeoVim");
    if (it != m_logger_names.end()) {
        m_selected_logger_index =
            static_cast<int>(std::distance(m_logger_names.begin(), it));
    }

    m_logger_names_populated = true;
}

void OutputWidget::_render_toolbar() {
    _populate_logger_names();

    // -- Logger combo --
    float combo_width = 80.0f;
    if (!m_logger_names.empty()) {
        for (const auto& name : m_logger_names) {
            float w = ImGui::CalcTextSize(name.c_str()).x;
            if (w > combo_width) {
                combo_width = w;
            }
        }
        combo_width += ImGui::GetStyle().FramePadding.x * 2.0f + 30.0f;
    }

    ImGui::SetNextItemWidth(combo_width);
    if (m_logger_names.empty()) {
        ImGui::BeginDisabled();
        ImGui::Button("No loggers", ImVec2(combo_width, 0));
        ImGui::EndDisabled();
    } else {
        if (m_selected_logger_index < 0 ||
            m_selected_logger_index >=
                static_cast<int>(m_logger_names.size())) {
            m_selected_logger_index = 0;
        }
        const char* preview =
            m_logger_names[static_cast<size_t>(m_selected_logger_index)]
                .c_str();
        if (ImGui::BeginCombo("##logger_filter", preview)) {
            for (size_t i = 0; i < m_logger_names.size(); ++i) {
                bool is_selected =
                    (static_cast<int>(i) == m_selected_logger_index);
                if (ImGui::Selectable(m_logger_names[i].c_str(), is_selected)) {
                    m_selected_logger_index = static_cast<int>(i);
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SameLine();

    // -- Filter input --
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x -
                            ImGui::CalcTextSize("Clear").x -
                            ImGui::CalcTextSize("Auto-scroll").x -
                            ImGui::GetStyle().ItemSpacing.x * 3 -
                            ImGui::GetStyle().FramePadding.x * 2);
    ImGui::InputTextWithHint("##output_filter", "Filter...", m_filter_buffer,
                             sizeof(m_filter_buffer));

    ImGui::SameLine();

    // -- Clear button --
    if (ImGui::Button("Clear")) {
        _clear_output();
    }

    ImGui::SameLine();

    // -- Auto-scroll toggle --
    ImGui::Checkbox("Auto-scroll", &m_auto_scroll);
}

// ---------------------------------------------------------------------------
// Log entry list
// ---------------------------------------------------------------------------

void OutputWidget::_render_log_entries() {
    // Collect new entries from the capture ring buffer.
    auto new_entries = ImApp::OutputCapture::instance().get_entries();
    if (!new_entries.empty()) {
        m_entries.insert(m_entries.end(),
                         std::make_move_iterator(new_entries.begin()),
                         std::make_move_iterator(new_entries.end()));

        // Keep local buffer bounded (same limit as the capture ring).
        while (m_entries.size() > ImApp::OutputCapture::g_max_entries) {
            m_entries.pop_front();
        }
    }

    // Build the filter string for case-insensitive matching.
    std::string filter{m_filter_buffer};
    bool has_filter = !filter.empty();
    if (has_filter) {
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }

    // Clear selection when the filter text changes (stale indices).
    if (filter != m_last_filter) {
        m_selected_indices.clear();
        m_last_filter = filter;
    }

    // Build a vector of pointers to visible entries (filtered by logger
    // and text filter).
    std::vector<const ImApp::LogEntry*> visible_entries;
    visible_entries.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        // Logger filter (combo box)
        if (!m_logger_names.empty() && m_selected_logger_index >= 0 &&
            m_selected_logger_index < static_cast<int>(m_logger_names.size())) {
            if (entry.logger_name !=
                m_logger_names[static_cast<size_t>(m_selected_logger_index)]) {
                continue;
            }
        }

        if (has_filter) {
            std::string lower_msg = entry.payload;
            std::transform(lower_msg.begin(), lower_msg.end(),
                           lower_msg.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower_msg.find(filter) == std::string::npos) {
                continue;
            }
        }
        visible_entries.push_back(&entry);
    }

    // Reserve enough height for the remaining area.
    ImGui::BeginChild("##output_log_region", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (visible_entries.empty()) {
        // Render a placeholder so the child region has measurable height.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
        ImGui::TextUnformatted(has_filter ? "No matching entries."
                                          : "No output yet.");
        ImGui::PopStyleColor();
    } else {
        // Multi-select: allows Shift+click range, Ctrl+click toggle,
        // and Ctrl+A select-all out of the box.
        ImGuiMultiSelectFlags ms_flags =
            ImGuiMultiSelectFlags_NoSelectOnRightClick |
            ImGuiMultiSelectFlags_ClearOnClickVoid |
            ImGuiMultiSelectFlags_BoxSelect1d;

        ImGui::BeginMultiSelect(ms_flags);

        for (int i = 0; i < static_cast<int>(visible_entries.size()); ++i) {
            ImGui::PushID(i);
            ImGui::SetNextItemSelectionUserData(i);
            bool selected = m_selected_indices.count(i) != 0;

            const auto* entry = visible_entries[static_cast<size_t>(i)];
            ImVec4 color = _color_for_level(entry->level);

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushStyleColor(ImGuiCol_Header,
                                  ImVec4(0.35f, 0.35f, 0.35f, 1.0f));

            const char* text = m_show_timestamps ? entry->message.c_str()
                                                 : entry->payload.c_str();

            if (ImGui::Selectable(text, selected,
                                  ImGuiSelectableFlags_AllowOverlap)) {
                // Selection state is handled by BeginMultiSelect /
                // EndMultiSelect — no manual toggle needed here.
            }

            // Right-click on a line opens the context menu.
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup("##output_context");
            }

            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            ImGui::PopID();
        }

        ImGuiMultiSelectIO* ms_io = ImGui::EndMultiSelect();
        _apply_multi_select(ms_io, static_cast<int>(visible_entries.size()));
    }

    // Ctrl+C shortcut: copy selected lines to the clipboard.
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
        if (!m_selected_indices.empty() && !visible_entries.empty()) {
            _copy_selected(visible_entries);
        }
    }

    // Context menu (also opens via right-click on the empty region).
    _render_context_menu(visible_entries);

    // Auto-scroll: if enabled, keep the scroll position at the bottom
    // when new entries arrive.
    if (m_auto_scroll && !new_entries.empty()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Colour mapping: spdlog level → ImGui colour
// ---------------------------------------------------------------------------

ImVec4 OutputWidget::_color_for_level(int level) {
    switch (static_cast<spdlog::level::level_enum>(level)) {
    case spdlog::level::trace:
        return ImVec4(0.50f, 0.50f, 0.50f, 1.00f); // Gray
    case spdlog::level::debug:
        return ImVec4(0.00f, 0.75f, 0.75f, 1.00f); // Cyan
    case spdlog::level::info:
        return ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // White
    case spdlog::level::warn:
        return ImVec4(1.00f, 1.00f, 0.00f, 1.00f); // Yellow
    case spdlog::level::err:
        return ImVec4(1.00f, 0.30f, 0.30f, 1.00f); // Red
    case spdlog::level::critical:
        return ImVec4(1.00f, 0.00f, 0.00f, 1.00f); // Bright red
    default:
        return ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // White
    }
}

// ---------------------------------------------------------------------------
// Multi-select helpers
// ---------------------------------------------------------------------------

void OutputWidget::_apply_multi_select(ImGuiMultiSelectIO* ms_io,
                                       int item_count) {
    if (ms_io == nullptr) {
        return;
    }

    for (const auto& req : ms_io->Requests) {
        switch (req.Type) {
        case ImGuiSelectionRequestType_SetAll:
            m_selected_indices.clear();
            if (req.Selected) {
                for (int i = 0; i < item_count; ++i) {
                    m_selected_indices.insert(i);
                }
            }
            break;

        case ImGuiSelectionRequestType_SetRange: {
            int first = req.RangeFirstItem;
            int last = req.RangeLastItem;
            if (first > last) {
                std::swap(first, last);
            }
            for (int i = first; i <= last; ++i) {
                if (req.Selected) {
                    m_selected_indices.insert(i);
                } else {
                    m_selected_indices.erase(i);
                }
            }
            break;
        }

        default:
            break;
        }
    }
}

void OutputWidget::_copy_selected(
    const std::vector<const ImApp::LogEntry*>& visible_entries) {
    if (m_selected_indices.empty() || visible_entries.empty()) {
        return;
    }

    std::string clipboard_text;
    clipboard_text.reserve(m_selected_indices.size() * 128);

    // m_selected_indices is a std::set<int>, so iteration is in sorted
    // order — the copied text appears in the same order as on screen.
    for (int idx : m_selected_indices) {
        if (idx >= 0 && idx < static_cast<int>(visible_entries.size())) {
            clipboard_text += m_show_timestamps ? visible_entries[idx]->message
                                                : visible_entries[idx]->payload;
            clipboard_text += '\n';
        }
    }

    ImGui::SetClipboardText(clipboard_text.c_str());
}

void OutputWidget::_clear_output() {
    // If a specific logger is selected, only clear its entries.
    if (!m_logger_names.empty() && m_selected_logger_index >= 0 &&
        m_selected_logger_index < static_cast<int>(m_logger_names.size())) {
        const auto& logger_name =
            m_logger_names[static_cast<size_t>(m_selected_logger_index)];
        ImApp::OutputCapture::instance().clear(logger_name);
        std::erase_if(m_entries, [&](const ImApp::LogEntry& e) {
            return e.logger_name == logger_name;
        });
    } else {
        ImApp::OutputCapture::instance().clear();
        m_entries.clear();
        m_selected_logger_index = 0;
    }
    m_selected_indices.clear();
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

void OutputWidget::_render_context_menu(
    const std::vector<const ImApp::LogEntry*>& visible_entries) {
    // Allow right-click on the empty child-region background to also open
    // the popup (in addition to right-clicking a Selectable line).
    // Use a simple hover+click test rather than BeginPopupContextWindow,
    // because the latter begins its own popup window which would corrupt
    // the window stack when combined with the manual BeginPopup below.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                               ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("##output_context");
    }

    if (!ImGui::BeginPopup("##output_context")) {
        return;
    }

    bool has_selection = !m_selected_indices.empty();

#ifdef IM_APP_DARWIN
    if (ImGui::MenuItem("Copy", "Cmd+C", false, has_selection)) {
#else
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, has_selection)) {
#endif
        _copy_selected(visible_entries);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Clear Output")) {
        _clear_output();
    }

    ImGui::Separator();

    ImGui::MenuItem("Show Timestamps", nullptr, &m_show_timestamps);

    ImGui::EndPopup();
}

} // namespace ImNeovim
