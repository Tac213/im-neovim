#include "im_app/application.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(IM_APP_DEBUG)
static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}
#endif

int main(int argc, char** argv) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
#if defined(IM_APP_DEBUG)
        fprintf(stderr, "Failed to initialize glfw!\n");
#endif
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    auto* app = ImApp::create_im_app(argc, argv);
    auto returncode = app->exec();
    delete app;
    glfwTerminate();
    return returncode;
}
