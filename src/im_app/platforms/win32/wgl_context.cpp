#include "wgl_context.h"
#include "win32_window.h"
#include <GL/glew.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace ImApp {
#if defined(_DEBUG)
static void GLAPIENTRY debug_callback(GLenum source, GLenum type, GLuint id,
                                      GLenum severity, GLsizei length,
                                      const GLchar* message,
                                      const void* user_param) {
    if (type == GL_DEBUG_TYPE_ERROR) {
        spdlog::error("WGL Error: {}", message);
    }
}
#endif

static std::shared_ptr<WGLContext> g_instance = nullptr;

WGLContext::WGLContext(std::shared_ptr<Window> window) {
    auto win32_window = std::static_pointer_cast<Win32Window>(window);
    m_hwnd = win32_window->get_hwnd();
}

void WGLContext::initialize() {
    if (!create_device(m_hwnd, m_hdc)) {
        cleanup_device(m_hwnd, m_hdc);
        return;
    }

    ::wglMakeCurrent(m_hdc, m_hrc);

    // Initialize GLEW
    GLenum init_result = glewInit();
    if (init_result != GLEW_OK) {
        throw std::runtime_error("[WGLContext] Failed to initialize GLEW.");
    }

    // Checking GL version
    glGetIntegerv(GL_MAJOR_VERSION, &m_major_version);
    glGetIntegerv(GL_MINOR_VERSION, &m_minor_version);
#if defined(_DEBUG)
    const GLubyte* gl_version_string = glGetString(GL_VERSION);
    const GLubyte* gl_renderer = glGetString(GL_RENDERER);

    spdlog::info("OpenGL is intialized, version: {}.{} context({}, {})\n",
                 m_major_version, m_minor_version,
                 reinterpret_cast<const char*>(gl_version_string),
                 reinterpret_cast<const char*>(gl_renderer));

    if (m_major_version >= 4 && m_minor_version >= 3) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(debug_callback, nullptr);
    }
#endif
}

void WGLContext::finalize() {
    cleanup_device(m_hwnd, m_hdc);
    ::wglDeleteContext(m_hrc);
}

void WGLContext::swap_buffers() { ::SwapBuffers(m_hdc); }

bool WGLContext::create_device(HWND hwnd, HDC& hdc) {
    HDC temp_hdc = ::GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;

    const int pf = ::ChoosePixelFormat(temp_hdc, &pfd);
    if (pf == 0) {
        spdlog::error("[WGLContext] Failed to choose pixel format.");
        return false;
    }
    if (::SetPixelFormat(temp_hdc, pf, &pfd) == FALSE) {
        spdlog::error("[WGLContext] Failed to set pixel format.");
        return false;
    }
    ::ReleaseDC(hwnd, temp_hdc);

    hdc = ::GetDC(hwnd);
    if (!m_hrc) {
        m_hrc = ::wglCreateContext(hdc);
    }
    return true;
}

void WGLContext::cleanup_device(HWND hwnd, HDC& hdc) {
    ::wglMakeCurrent(nullptr, nullptr);
    ::ReleaseDC(hwnd, hdc);
}

bool WGLContext::make_current(HDC& hdc) { return ::wglMakeCurrent(hdc, m_hrc); }

bool WGLContext::make_current() { return ::wglMakeCurrent(m_hdc, m_hrc); }

bool WGLContext::swap_buffers(HDC& hdc) { return ::SwapBuffers(hdc); }
} // namespace ImApp