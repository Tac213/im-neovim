#include "im_neovim/gui/about_panel.h"

#include "im_neovim/build_config.h"
#include <fmt/format.h>
#include <im_app/application.h>
#include <im_app/image_manager.h>
#include <imgui.h>

#ifdef IM_APP_WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#ifdef IM_APP_LINUX
#include <sys/utsname.h>
#endif

#ifdef IM_APP_DARWIN
#include <sys/sysctl.h>
#include <sys/utsname.h>
#endif

namespace ImNeovim {

AboutPanel::~AboutPanel() {
    if (m_icon_loaded) {
        ImApp::ImageManager::free_image(m_icon);
    }
}

void AboutPanel::show() {
    // Only set the flag. Actual ImGui calls happen in render() during the
    // render phase, after ImGui::NewFrame().
    m_active = true;
}

void AboutPanel::render(const std::string& nvim_version) {
    if (!m_active) {
        return;
    }

    // OpenPopup must be called every frame before BeginPopupModal.
    if (!ImGui::IsPopupOpen("##About")) {
        ImGui::OpenPopup("##About");
    }

    // Center the modal on screen with a minimum width to prevent overlap.
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("##About", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        // --- Icon (left) and Title (right) ---
        // Lazy-load the icon on first render.
        if (!m_icon_loaded) {
            m_icon = ImApp::ImageManager::load(":/imnvim/assets/nvim.png");
            m_icon_loaded = true;
        }

        if (m_icon.texture_id != 0) {
            ImGui::Image(static_cast<ImTextureID>(m_icon.texture_id),
                         ImVec2(64, 64));
            ImGui::SameLine();
        }

        // Title
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16);
        ImGui::TextUnformatted("ImNeovim");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- Information lines ---
        // Two-column table: label column auto-sizes, value column fills
        // remaining space.
        if (ImGui::BeginTable("##InfoTable", 2,
                              ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Value",
                                    ImGuiTableColumnFlags_WidthStretch);

            auto info_row = [](const char* label, const char* value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(label);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value);
            };

            info_row("Version:", IMNVIM_VERSION);
            info_row("Commit SHA:", IMNVIM_GIT_SHA);
            info_row("Build Date:", IMNVIM_BUILD_DATE);

            const char* nvim_ver =
                nvim_version.empty() ? "Connecting..." : nvim_version.c_str();
            info_row("Bundled Neovim:", nvim_ver);

            info_row("ImGui Version:", IMGUI_VERSION);

            {
                std::string compiler = std::string(IMNVIM_COMPILER_ID) + " " +
                                       IMNVIM_COMPILER_VERSION;
                info_row("Compiler:", compiler.c_str());
            }

            info_row("Graphics:", IM_APP.get_backend_name());

            {
                std::string os = _get_os_version();
                info_row("OS:", os.c_str());
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- Buttons ---
        float button_width = ImGui::GetFontSize() * 7.0f;

        // Right-align buttons
        float avail_width = ImGui::GetContentRegionAvail().x;
        float total_buttons =
            button_width * 2 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(avail_width - total_buttons +
                             ImGui::GetStyle().WindowPadding.x);

        if (ImGui::Button("Copy", ImVec2(button_width, 0))) {
            _copy_to_clipboard(_collect_info_text(nvim_version));
        }
        ImGui::SameLine();
        if (ImGui::Button("OK", ImVec2(button_width, 0))) {
            m_active = false;
            ImGui::CloseCurrentPopup();
        }

        // Also close on Escape.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_active = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    } else {
        // Popup was closed externally (e.g. clicking outside). Reset state.
        m_active = false;
    }
}

std::string AboutPanel::_collect_info_text(const std::string& nvim_version) {
    std::string text;
    auto add_line = [&text](const char* label, const std::string& value) {
        text += label;
        text += " ";
        text += value;
        text += "\n";
    };

    add_line("Version:", IMNVIM_VERSION);
    add_line("Commit SHA:", IMNVIM_GIT_SHA);
    add_line("Build Date:", IMNVIM_BUILD_DATE);

    std::string nv = nvim_version.empty() ? "Connecting..." : nvim_version;
    add_line("Bundled Neovim:", nv);

    add_line("ImGui Version:", IMGUI_VERSION);

    std::string compiler =
        std::string(IMNVIM_COMPILER_ID) + " " + IMNVIM_COMPILER_VERSION;
    add_line("Compiler:", compiler);

    add_line("Graphics:", IM_APP.get_backend_name());

    add_line("OS:", _get_os_version());

    return text;
}

void AboutPanel::_copy_to_clipboard(const std::string& text) {
    ImGui::SetClipboardText(text.c_str());
}

std::string AboutPanel::_get_os_version() {
#ifdef IM_APP_WIN32
    // Use RtlGetVersion for accurate Windows version (GetVersionEx lies
    // without a compatibility manifest).
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        using RtlGetVersionPtr = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        auto rtl_get_version = reinterpret_cast<RtlGetVersionPtr>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtl_get_version) {
            RTL_OSVERSIONINFOW info = {};
            info.dwOSVersionInfoSize = sizeof(info);
            if (rtl_get_version(&info) == 0) {
                return fmt::format("Windows {}.{}.{}", info.dwMajorVersion,
                                   info.dwMinorVersion, info.dwBuildNumber);
            }
        }
    }
    return "Windows (unknown)";
#elif defined(IM_APP_DARWIN)
    // Use sysctlbyname to get the macOS marketing version (e.g. "15.2").
    char osversion[256] = {};
    size_t len = sizeof(osversion);
    if (sysctlbyname("kern.osproductversion", osversion, &len, nullptr, 0) ==
            0 &&
        len > 0) {
        return "macOS " + std::string(osversion);
    }
    // Fallback to uname.
    struct utsname info{};
    if (uname(&info) == 0) {
        return fmt::format("{} {}", info.sysname, info.release);
    }
    return "macOS (unknown)";
#elif defined(IM_APP_LINUX)
    struct utsname info{};
    if (uname(&info) == 0) {
        return fmt::format("{} {}", info.sysname, info.release);
    }
    return "Linux (unknown)";
#else
    return "Unknown OS";
#endif
}

} // namespace ImNeovim
