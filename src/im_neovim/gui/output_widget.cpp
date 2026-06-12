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

void OutputWidget::_render_toolbar() {
    // -- Filter input --
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x -
                            ImGui::CalcTextSize("Clear").x -
                            ImGui::CalcTextSize("Auto-scroll").x -
                            ImGui::GetStyle().ItemSpacing.x * 4 -
                            ImGui::GetStyle().FramePadding.x * 2);
    ImGui::InputTextWithHint("##output_filter", "Filter...", m_filter_buffer,
                             sizeof(m_filter_buffer));

    ImGui::SameLine();

    // -- Clear button --
    if (ImGui::Button("Clear")) {
        ImApp::OutputCapture::instance().clear();
        m_entries.clear();
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

    // Pre-filter: build a vector of pointers to visible entries so that
    // ImGuiListClipper always sees an item on the first row (its height
    // measurement fails with an assertion if all items in a batch are
    // skipped via `continue`).
    std::vector<const ImApp::LogEntry*> visible_entries;
    visible_entries.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        if (has_filter) {
            std::string lower_msg = entry.message;
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
        // Still need to render at least one item so that the child region
        // has a measurable height.  A dim placeholder line suffices.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
        ImGui::TextUnformatted(has_filter ? "No matching entries."
                                          : "No output yet.");
        ImGui::PopStyleColor();
    } else {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_entries.size()));

        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto* entry = visible_entries[static_cast<size_t>(i)];
                ImVec4 color = _color_for_level(entry->level);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(entry->message.c_str());
                ImGui::PopStyleColor();
            }
        }
    }

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

} // namespace ImNeovim
