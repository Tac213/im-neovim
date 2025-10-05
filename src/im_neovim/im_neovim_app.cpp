#include <im_app/application.h>
#include <im_app/layer.h>
#include <imgui.h>

namespace ImNeovim {
class MyLayer : public ImApp::Layer {
  public:
    void on_imgui_render() override { ImGui::ShowDemoWindow(); }
};
} // namespace ImNeovim

namespace ImApp {
Application* create_im_app(int argc, char** argv) {
    AppSpec app_spec{.main_window_no_border = false};
    auto* app = new Application(app_spec);
    app->push_layer<ImNeovim::MyLayer>();
    return app;
}
} // namespace ImApp
