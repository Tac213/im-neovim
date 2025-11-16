#include "im_app/application.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#if defined(IM_APP_DEBUG)
static void glfw_error_callback(int error, const char* description) {
    spdlog::error("GLFW Error {}: {}", error, description);
}
#endif

int main(int argc, char** argv) {
#if defined(IM_APP_DEBUG)
    glfwSetErrorCallback(glfw_error_callback);
#endif
    if (!glfwInit()) {
        spdlog::error("Failed to initialize glfw!");
        return 1;
    }
    auto* app = ImApp::create_im_app(argc, argv);
    auto returncode = app->exec();
    delete app;
    glfwTerminate();
    return returncode;
}
