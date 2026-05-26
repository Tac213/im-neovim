#include "glfw_context.h"
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace ImApp {
#if defined(IM_APP_DEBUG)
static void GLAPIENTRY debug_callback(GLenum source, GLenum type, GLuint id,
                                      GLenum severity, GLsizei length,
                                      const GLchar* message,
                                      const void* user_param) {
    if (type == GL_DEBUG_TYPE_ERROR) {
        spdlog::error("GL Error: {}", message);
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

    spdlog::info("OpenGL is intialized, version: {}.{} context({}, {})",
                 m_major_version, m_minor_version,
                 reinterpret_cast<const char*>(gl_version_string),
                 reinterpret_cast<const char*>(gl_renderer));

    m_backend_name =
        fmt::format("OpenGL {}.{} (GLFW)", m_major_version, m_minor_version);

    if (m_major_version >= 4 && m_minor_version >= 3) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(debug_callback, nullptr);
    }
#endif
}

void GlfwContext::finalize() {}

const char* GlfwContext::get_backend_name() const {
    return m_backend_name.c_str();
}

void GlfwContext::swap_buffers() {
    if (m_window) {
        glfwSwapBuffers(m_window->get_glfw_window());
    }
}

uint64_t GlfwContext::create_texture(const uint8_t* pixels, uint32_t width,
                                     uint32_t height) {
    GLuint gl_tex = 0;
    glGenTextures(1, &gl_tex);
    if (gl_tex == 0) {
        spdlog::error("[GlfwContext] glGenTextures failed");
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, gl_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return static_cast<uint64_t>(gl_tex);
}

void GlfwContext::destroy_texture(uint64_t texture_id) {
    GLuint gl_tex = static_cast<GLuint>(texture_id);
    glDeleteTextures(1, &gl_tex);
}

std::shared_ptr<GraphicsContext>
GraphicsContext::create(std::shared_ptr<Window> window,
                        GraphicsBackend backend) {
    auto glfw_window = std::static_pointer_cast<GlfwWindow>(window);
    auto context = std::make_shared<GlfwContext>(glfw_window);
    return context;
}
} // namespace ImApp
