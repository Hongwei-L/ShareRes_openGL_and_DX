#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <DirectXMath.h>
#include <assert.h>
#include "glew.h"
#include "wglew.h"
#include <chrono>
#include <d3dcompiler.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")

#define SCREEN_WIDTH  300
#define SCREEN_HEIGHT 300
#define KEYDOWN(vk) (GetAsyncKeyState(vk) & 0x8000)

HWND g_hWndDX = nullptr;
HWND g_hWndGL = nullptr;

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pImmediateContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;

ID3D11Texture2D* g_pSharedTex = nullptr;
HANDLE g_hSharedHandle = nullptr;

GLuint g_GLTexture = 0;
HANDLE g_hGLShared = nullptr;
HANDLE g_hDXDevice = nullptr;
HDC g_hDCGL = nullptr;

struct SimpleVertex
{
    XMFLOAT3 Pos;
    XMFLOAT4 Color;
};

ComPtr<ID3D11Buffer> g_pVertexBuffer;
ComPtr<ID3D11VertexShader> g_pVertexShader;
ComPtr<ID3D11PixelShader> g_pPixelShader;
ComPtr<ID3D11InputLayout> g_pVertexLayout;
ComPtr<ID3D11RenderTargetView> g_pSharedRTV;
ComPtr<ID3D11Buffer> g_pConstantBuffer;

// ===== D3D11 shader (HLSL embedded) =====
const char* g_VS =
"cbuffer MatrixBuffer : register(b0) { matrix mWorldViewProj; };"
"struct VS_INPUT { float3 Pos : POSITION; float4 Col : COLOR; };"
"struct PS_INPUT { float4 Pos : SV_POSITION; float4 Col : COLOR; };"
"PS_INPUT main(VS_INPUT input) {"
"  PS_INPUT o;"
"  o.Pos = mul(float4(input.Pos,1.0f), mWorldViewProj);"
"  o.Col = input.Col;"
"  return o;"
"}";

const char* g_PS =
"struct PS_INPUT { float4 Pos : SV_POSITION; float4 Col : COLOR; };"
"float4 main(PS_INPUT input) : SV_Target { return input.Col; }";
// ========================================

void InitDX(HWND hWnd);
void InitGL(HWND hWnd);
void RenderDX();
void RenderGL();
void Destroy();
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

// -----------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    WNDCLASSEX wc{ sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WindowProc,
                   0,0,hInstance, nullptr, LoadCursor(NULL, IDC_ARROW), nullptr, nullptr,
                   L"WindowClass", nullptr };
    RegisterClassEx(&wc);

    g_hWndDX = CreateWindow(L"WindowClass", L"D3D11 Shared Texture",
        WS_OVERLAPPEDWINDOW, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
        nullptr, nullptr, hInstance, nullptr);
    g_hWndGL = CreateWindow(L"WindowClass", L"OpenGL Shared Texture",
        WS_OVERLAPPEDWINDOW, SCREEN_WIDTH + 20, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hWndDX, nCmdShow);
    ShowWindow(g_hWndGL, nCmdShow);

    InitDX(g_hWndDX);
    InitGL(g_hWndGL);

    MSG msg{};
    while (msg.message != WM_QUIT)
    {
        if (KEYDOWN(VK_ESCAPE))
            PostQuitMessage(0);

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

		//Sleep(1); // ~60 FPS

        RenderDX();
        RenderGL();
    }

    Destroy();
    return 0;
}

// -----------------------------------------
void InitDX(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = SCREEN_WIDTH;
    sd.BufferDesc.Height = SCREEN_HEIGHT;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pImmediateContext);

    // Create render target view
    ComPtr<ID3D11Texture2D> pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, &g_pRenderTargetView);

    // Create shared texture
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = SCREEN_WIDTH;
    desc.Height = SCREEN_HEIGHT;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    g_pd3dDevice->CreateTexture2D(&desc, nullptr, &g_pSharedTex);

    // Get shared handle
    ComPtr<IDXGIResource> dxgiRes;
    g_pSharedTex->QueryInterface(IID_PPV_ARGS(&dxgiRes));
    dxgiRes->GetSharedHandle(&g_hSharedHandle);

    // Create RTV for shared texture
    g_pd3dDevice->CreateRenderTargetView(g_pSharedTex, nullptr, &g_pSharedRTV);

    // Compile shaders
    ComPtr<ID3DBlob> vsBlob, psBlob;
    D3DCompile(g_VS, strlen(g_VS), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsBlob, nullptr);
    D3DCompile(g_PS, strlen(g_PS), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psBlob, nullptr);

    g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_pVertexShader);
    g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_pPixelShader);

    // Input layout
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0, D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}
    };
    g_pd3dDevice->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(), &g_pVertexLayout);

    // Create vertex buffer
    SimpleVertex vertices[] =
    {
        { XMFLOAT3(0.0f,  0.5f, 0.5f), XMFLOAT4(1,0,0,1)},
        { XMFLOAT3(0.5f, -0.5f, 0.5f), XMFLOAT4(0,1,0,1)},
        { XMFLOAT3(-0.5f,-0.5f, 0.5f), XMFLOAT4(0,0,1,1)},
    };

    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(SimpleVertex) * 3;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initData{ vertices };
    g_pd3dDevice->CreateBuffer(&bd, &initData, &g_pVertexBuffer);

    // Create viewport
    D3D11_VIEWPORT vp{};
    vp.Width = (FLOAT)SCREEN_WIDTH;
    vp.Height = (FLOAT)SCREEN_HEIGHT;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pImmediateContext->RSSetViewports(1, &vp);

    // Constant buffer (matrix)
    XMMATRIX identity = XMMatrixIdentity();
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.ByteWidth = sizeof(XMMATRIX);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cbInit{ &identity };
    g_pd3dDevice->CreateBuffer(&cbDesc, &cbInit, &g_pConstantBuffer);
}

void InitGL(HWND hWnd)
{
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

    g_hDCGL = GetDC(hWnd);
    int pixelFormat = ChoosePixelFormat(g_hDCGL, &pfd);
    SetPixelFormat(g_hDCGL, pixelFormat, &pfd);
    HGLRC hRC = wglCreateContext(g_hDCGL);
    wglMakeCurrent(g_hDCGL, hRC);
    glewInit();

    if (WGLEW_NV_DX_interop2)
    {
        g_hDXDevice = wglDXOpenDeviceNV(g_pd3dDevice);
        glGenTextures(1, &g_GLTexture);

        wglDXSetResourceShareHandleNV(g_pSharedTex, g_hSharedHandle);
        g_hGLShared = wglDXRegisterObjectNV(g_hDXDevice,
            g_pSharedTex, g_GLTexture, GL_TEXTURE_2D, WGL_ACCESS_READ_ONLY_NV);
    }

    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, -1, 1);
    glDisable(GL_DEPTH_TEST);
}

void RenderDX()
{
    static float angle = 0.0f;
    angle += 0.02f;
    XMMATRIX mWorldViewProj = XMMatrixTranspose(XMMatrixRotationZ(angle));

    g_pImmediateContext->UpdateSubresource(g_pConstantBuffer.Get(), 0, nullptr, &mWorldViewProj, 0, 0);
    g_pImmediateContext->VSSetConstantBuffers(0, 1, g_pConstantBuffer.GetAddressOf());

    float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
    g_pImmediateContext->OMSetRenderTargets(1, g_pSharedRTV.GetAddressOf(), nullptr);
    g_pImmediateContext->ClearRenderTargetView(g_pSharedRTV.Get(), clearColor);

    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    g_pImmediateContext->IASetInputLayout(g_pVertexLayout.Get());
    g_pImmediateContext->IASetVertexBuffers(0, 1, g_pVertexBuffer.GetAddressOf(), &stride, &offset);
    g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pImmediateContext->VSSetShader(g_pVertexShader.Get(), nullptr, 0);
    g_pImmediateContext->PSSetShader(g_pPixelShader.Get(), nullptr, 0);
    g_pImmediateContext->Draw(3, 0);
    g_pImmediateContext->Flush();

    //g_pSwapChain->Present(1, 0);

}

void RenderGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    if (g_hDXDevice && g_hGLShared)
    {
        wglDXLockObjectsNV(g_hDXDevice, 1, &g_hGLShared);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_GLTexture);

        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(0, 0);
        glTexCoord2f(0, 1); glVertex2f(0, SCREEN_HEIGHT);
        glTexCoord2f(1, 1); glVertex2f(SCREEN_WIDTH, SCREEN_HEIGHT);
        glTexCoord2f(1, 0); glVertex2f(SCREEN_WIDTH, 0);
        glEnd();
        wglDXUnlockObjectsNV(g_hDXDevice, 1, &g_hGLShared);

        SwapBuffers(g_hDCGL);

    }
}

void Destroy()
{
    if (WGLEW_NV_DX_interop2)
    {
        if (g_hGLShared) wglDXUnregisterObjectNV(g_hDXDevice, g_hGLShared);
        if (g_hDXDevice) wglDXCloseDeviceNV(g_hDXDevice);
    }
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}


