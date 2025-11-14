#include "OpenGLSharedRenderer.h"

#include "wglew.h"

OpenGLSharedRenderer::OpenGLSharedRenderer(int width, int height)
    : m_width(width)
    , m_height(height)
    , m_hwnd(nullptr)
    , m_hdc(nullptr)
    , m_context(nullptr)
    , m_glTexture(0)
    , m_glSharedHandle(nullptr)
    , m_dxDeviceHandle(nullptr)
    , m_sharedTexture(nullptr)
    , m_sharedHandle(nullptr)
{
}

OpenGLSharedRenderer::~OpenGLSharedRenderer()
{
    Cleanup();
}

bool OpenGLSharedRenderer::Initialize(HWND hwnd)
{
    if (!hwnd)
    {
        return false;
    }

    static PIXELFORMATDESCRIPTOR pfd =
    {
        sizeof(PIXELFORMATDESCRIPTOR),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA,
        32,
        0,0,0,0,0,0,
        0,0,0,0,0,0,0,
        24,0,0,
        PFD_MAIN_PLANE,0,0,0,0
    };

    m_hwnd = hwnd;
    m_hdc = GetDC(hwnd);

    int pixelFormat = ChoosePixelFormat(m_hdc, &pfd);
    if (pixelFormat == 0)
    {
        return false;
    }

    if (!SetPixelFormat(m_hdc, pixelFormat, &pfd))
    {
        return false;
    }

    m_context = wglCreateContext(m_hdc);
    if (!m_context)
    {
        return false;
    }

    if (!wglMakeCurrent(m_hdc, m_context))
    {
        return false;
    }

    if (glewInit() != GLEW_OK)
    {
        return false;
    }

    ConfigureViewport();
    return true;
}

bool OpenGLSharedRenderer::SetupSharedTexture(ID3D11Device* device, ID3D11Texture2D* sharedTexture, HANDLE sharedHandle)
{
    if (!device || !sharedTexture || !sharedHandle)
    {
        return false;
    }

    if (!WGLEW_NV_DX_interop2)
    {
        return false;
    }

    ReleaseSharedResources();

    m_sharedTexture = sharedTexture;
    m_sharedTexture->AddRef();
    m_sharedHandle = sharedHandle;

    m_dxDeviceHandle = wglDXOpenDeviceNV(device);
    if (!m_dxDeviceHandle)
    {
        return false;
    }

    glGenTextures(1, &m_glTexture);
    wglDXSetResourceShareHandleNV(m_sharedTexture, m_sharedHandle);
    m_glSharedHandle = wglDXRegisterObjectNV(
        m_dxDeviceHandle,
        m_sharedTexture,
        m_glTexture,
        GL_TEXTURE_2D,
        WGL_ACCESS_READ_ONLY_NV);

    if (!m_glSharedHandle)
    {
        ReleaseSharedResources();
        return false;
    }

    return true;
}

void OpenGLSharedRenderer::Render()
{
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_dxDeviceHandle || !m_glSharedHandle)
    {
        return;
    }

    wglDXLockObjectsNV(m_dxDeviceHandle, 1, &m_glSharedHandle);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_glTexture);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(0, 1); glVertex2f(0, static_cast<GLfloat>(m_height));
    glTexCoord2f(1, 1); glVertex2f(static_cast<GLfloat>(m_width), static_cast<GLfloat>(m_height));
    glTexCoord2f(1, 0); glVertex2f(static_cast<GLfloat>(m_width), 0);
    glEnd();

    wglDXUnlockObjectsNV(m_dxDeviceHandle, 1, &m_glSharedHandle);

    if (m_hdc)
    {
        SwapBuffers(m_hdc);
    }
}

void OpenGLSharedRenderer::Cleanup()
{
    ReleaseSharedResources();

    if (m_context)
    {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_context);
        m_context = nullptr;
    }

    if (m_hwnd && m_hdc)
    {
        ReleaseDC(m_hwnd, m_hdc);
    }

    m_hwnd = nullptr;
    m_hdc = nullptr;
}

void OpenGLSharedRenderer::ConfigureViewport()
{
    glViewport(0, 0, m_width, m_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, m_width, m_height, 0, -1, 1);
    glDisable(GL_DEPTH_TEST);
}

void OpenGLSharedRenderer::ReleaseSharedResources()
{
    if (m_glTexture != 0)
    {
        glDeleteTextures(1, &m_glTexture);
        m_glTexture = 0;
    }

    if (WGLEW_NV_DX_interop2)
    {
        if (m_glSharedHandle && m_dxDeviceHandle)
        {
            wglDXUnregisterObjectNV(m_dxDeviceHandle, m_glSharedHandle);
        }

        if (m_dxDeviceHandle)
        {
            wglDXCloseDeviceNV(m_dxDeviceHandle);
        }
    }

    if (m_sharedTexture)
    {
        m_sharedTexture->Release();
        m_sharedTexture = nullptr;
    }

    m_glSharedHandle = nullptr;
    m_dxDeviceHandle = nullptr;
    m_sharedHandle = nullptr;
}
