#pragma once

#include <Windows.h>
#include <d3d11.h>
#include "glew.h"

class OpenGLSharedRenderer
{
public:
    OpenGLSharedRenderer(int width, int height);
    ~OpenGLSharedRenderer();

    bool Initialize(HWND hwnd);
    bool SetupSharedTexture(ID3D11Device* device, ID3D11Texture2D* sharedTexture, HANDLE sharedHandle);
    void Render();
    void Cleanup();

private:
    void ConfigureViewport();
    void ReleaseSharedResources();

    int m_width;
    int m_height;
    HWND m_hwnd;
    HDC m_hdc;
    HGLRC m_context;
    GLuint m_glTexture;
    HANDLE m_glSharedHandle;
    HANDLE m_dxDeviceHandle;
    ID3D11Texture2D* m_sharedTexture;
    HANDLE m_sharedHandle;
    bool m_isStereoContext;
};
