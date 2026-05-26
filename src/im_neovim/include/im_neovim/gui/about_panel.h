#pragma once

#include <im_app/image_manager.h>
#include <imgui.h>
#include <string>

namespace ImNeovim {

/// About modal panel shown from Help > About.
/// Displays version info, build details, and system information.
class AboutPanel {
  public:
    AboutPanel() = default;
    ~AboutPanel();

    /// Show the about modal on the next frame.
    void show();

    /// Render the about modal. Call each frame while active.
    /// @param nvim_version  Neovim version string (empty if not yet
    /// connected).
    void render(const std::string& nvim_version);

  private:
    bool m_active{false};
    ImApp::Image m_icon{0, 0, 0};
    bool m_icon_loaded{false};

    /// Collect all panel text into a multi-line string for clipboard copy.
    static std::string _collect_info_text(const std::string& nvim_version);

    /// Copy text to the system clipboard.
    static void _copy_to_clipboard(const std::string& text);

    /// Get a human-readable OS version string (platform-specific).
    static std::string _get_os_version();
};

} // namespace ImNeovim
