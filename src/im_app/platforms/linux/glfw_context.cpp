#include "glfw_context.h"

namespace ImApp {
#if defined(IM_APP_DEBUG)
static void GLAPIENTRY debug_callback(GLenum source, GLenum type, GLuint id,
                                      GLenum severity, GLsizei length,
                                      const GLchar* message,
                                      const void* user_param) {
    if (type == GL_DEBUG_TYPE_ERROR) {
        fprintf(stderr, "GL Error: %s\n", message);
    }
}
#endif

GlfwContext::GlfwContext(std::shared_ptr<GlfwWindow> window)
    : m_window(window) {}

void GlfwContext::initialize() {
    if (!m_window) {
        return;
    }
    glfwMakeContextCurrent(m_window->get_glfw_window());
    glfwSwapInterval(1); // Enable vsync
    // Initialize GLEW
    GLenum init_result = glewInit();
    if (init_result != GLEW_OK) {
        throw std::runtime_error("[GlfwContext] Failed to initialize GLEW.");
    }

    // Checking GL version
    glGetIntegerv(GL_MAJOR_VERSION, &m_major_version);
    glGetIntegerv(GL_MINOR_VERSION, &m_minor_version);
#if defined(IM_APP_DEBUG)
    const GLubyte* gl_version_string = glGetString(GL_VERSION);
    const GLubyte* gl_renderer = glGetString(GL_RENDERER);

    fprintf(stdout, "OpenGL is intialized, version: %d.%d context(%s, %s)\n",
            m_major_version, m_minor_version,
            reinterpret_cast<const char*>(gl_version_string),
            reinterpret_cast<const char*>(gl_renderer));

    if (m_major_version >= 4 && m_minor_version >= 3) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(debug_callback, nullptr);
    }
#endif
}

void GlfwContext::finalize() {}

void GlfwContext::swap_buffers() {
    if (m_window) {
        glfwSwapBuffers(m_window->get_glfw_window());
    }
}

std::shared_ptr<GraphicsContext>
GraphicsContext::create(std::shared_ptr<Window> window) {
    auto glfw_window = std::static_pointer_cast<GlfwWindow>(window);
    auto context = std::make_shared<GlfwContext>(glfw_window);
    return context;
}
} // namespace ImApp
