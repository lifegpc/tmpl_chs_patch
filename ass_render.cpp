#include "ass_render.h"
#include <windows.h>
#include "proxy.h"
#include <d3dx9.h>
#include <ass/ass.h>
#include <chrono>
#include <thread>
#include "err.h"
#include "wchar_util.h"
#include "str_util.h"
#include "fileop.h"

struct SubtitleThreadParams {
    HWND parentHwnd;
    bool shouldStop;
    std::string fileName;
};

static HANDLE g_subtitleThread = nullptr;
static SubtitleThreadParams g_subtitleParams;

class SubtitleRenderer {
private:
    HWND m_hwnd;
    HWND m_parentHwnd;
    IDirect3D9* m_d3d;
    IDirect3DDevice9* m_device;
    IDirect3DTexture9* m_texture;
    IDirect3DSurface9* m_surface;
    ID3DXSprite* m_sprite;

    ASS_Library* m_assLibrary;
    ASS_Renderer* m_assRenderer;
    ASS_Track* m_assTrack;

    std::chrono::steady_clock::time_point m_startTime;
    int m_windowWidth;
    int m_windowHeight;
    std::string m_fileName;
    POINT last_pt;

public:
    SubtitleRenderer() : m_hwnd(nullptr), m_parentHwnd(nullptr), m_d3d(nullptr),
        m_device(nullptr), m_texture(nullptr), m_surface(nullptr), m_sprite(nullptr),
        m_assLibrary(nullptr), m_assRenderer(nullptr), m_assTrack(nullptr),
        m_windowWidth(1280), m_windowHeight(720) {}

    ~SubtitleRenderer() {
        Cleanup();
    }

    bool Initialize(HWND parentHwnd, std::string fileName) {
        m_parentHwnd = parentHwnd;
        m_fileName = fileName;

        // 获取父窗口尺寸
        RECT parentRect;
        GetClientRect(parentHwnd, &parentRect);
        m_windowWidth = parentRect.right;
        m_windowHeight = parentRect.bottom;

        if (!CreateSubtitleWindow()) return false;
        if (!InitializeD3D()) return false;
        if (!InitializeLibass()) return false;
        if (!LoadSubtitleFile()) return false;

        m_startTime = std::chrono::steady_clock::now();
        SetActiveWindow(m_parentHwnd);
        return true;
    }

    void printWinError(std::string basic) {
        DWORD errorCode = GetLastError();
        std::string goodMsg;
        if (!err::get_winerror(goodMsg, errorCode)) {
            goodMsg = "Unknown error";
        } else {
            goodMsg = str_util::str_trim(goodMsg);
        }
        std::string re = basic + ": " + goodMsg + "(" + std::to_string(errorCode) + ")";
        std::wstring wMsg;
        if (wchar_util::str_to_wstr(wMsg, re, CP_UTF8)) {
            MessageBoxW(m_parentHwnd, wMsg.c_str(), L"错误消息", MB_OK | MB_ICONERROR);
        } else {
            MessageBoxA(m_parentHwnd, re.c_str(), "Error", MB_OK | MB_ICONERROR);
        }
    }

    bool CreateSubtitleWindow() {
        // 注册窗口类
        WNDCLASSW wc = {};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SubtitleWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"TMPL_SubtitleWindow";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; // 透明背景

        RegisterClass(&wc);

        // 创建分层窗口（支持透明）
        m_hwnd = CreateWindowEx(
            WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOPARENTNOTIFY,
            L"TMPL_SubtitleWindow",
            L"TMPL_Subtitle",
            WS_POPUP | WS_VISIBLE,
            0, 0, m_windowWidth, m_windowHeight,
            m_parentHwnd,
            nullptr,
            GetModuleHandle(nullptr),
            this
        );

        if (!m_hwnd) {
            printWinError("CreateWindowExW failed");
            return false;
        }

        // 设置窗口透明度
        SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
        UpdateWindowPosition();
        return true;
    }

    void UpdateWindowPosition() {
        if (!m_parentHwnd || !m_hwnd) return;

        RECT parentRect;
        GetClientRect(m_parentHwnd, &parentRect);

        POINT pt = { 0, 0 };
        ClientToScreen(m_parentHwnd, &pt);

        last_pt = pt;

        SetWindowPos(m_hwnd, HWND_TOPMOST,
            pt.x, pt.y,
            parentRect.right, parentRect.bottom,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }

    void UpdatePosIfNeeded() {
        if (!m_parentHwnd) return;
        POINT pt = { 0, 0 };
        ClientToScreen(m_parentHwnd, &pt);
        if (pt.x != last_pt.x || pt.y != last_pt.y) {
            UpdateWindowPosition();
        }
    }

    bool InitializeD3D() {
        m_d3d = Proxy::OriginalDirect3DCreate9(D3D_SDK_VERSION);
        if (!m_d3d) return false;

        D3DPRESENT_PARAMETERS d3dpp = {};
        d3dpp.Windowed = TRUE;
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow = m_hwnd;
        d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
        d3dpp.BackBufferWidth = m_windowWidth;
        d3dpp.BackBufferHeight = m_windowHeight;
        d3dpp.EnableAutoDepthStencil = FALSE;

        HRESULT hr = m_d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            m_hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &d3dpp,
            &m_device
        );

        if (FAILED(hr)) return false;

        // 创建纹理用于字幕渲染
        hr = m_device->CreateTexture(
            m_windowWidth, m_windowHeight, 1, D3DUSAGE_DYNAMIC,
            D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_texture, nullptr
        );

        if (FAILED(hr)) return false;

        // 创建Sprite用于渲染
        hr = D3DXCreateSprite(m_device, &m_sprite);
        if (FAILED(hr)) return false;

        // 设置渲染状态
        m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        m_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        m_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

        return true;
    }

    bool InitializeLibass() {
        m_assLibrary = ass_library_init();
        if (!m_assLibrary) return false;

        ass_set_message_cb(m_assLibrary, nullptr, nullptr);

        m_assRenderer = ass_renderer_init(m_assLibrary);
        if (!m_assRenderer) return false;

        // 设置渲染参数
        ass_set_frame_size(m_assRenderer, m_windowWidth, m_windowHeight);
        ass_set_margins(m_assRenderer, 20, 20, 20, 20);
        ass_set_use_margins(m_assRenderer, 1);
        ass_set_font_scale(m_assRenderer, 1.0);

        // 设置字体
        ass_set_fonts(m_assRenderer, nullptr, "sans-serif", 1, nullptr, 1);
        ass_set_fonts_dir(m_assLibrary, "fonts");

        return true;
    }

    bool LoadSubtitleFile() {
        std::wstring fName;
        wchar_util::str_to_wstr(fName, m_fileName, CP_UTF8);
        HANDLE hFile = CreateFileW(fName.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, NULL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD fileSize = GetFileSize(hFile, NULL);
        char* buf = new char[fileSize];
        DWORD readed = 0;
        if (!ReadFile(hFile, buf, fileSize, &readed, NULL)) {
            delete[] buf;
            return false;
        }
        m_assTrack = ass_read_memory(m_assLibrary, buf, readed, "UTF-8");
        delete[] buf;
        if (!m_assTrack) {
            return false;
        }
        return true;
    }

    void RenderSubtitle() {
        if (!m_device || !m_assRenderer || !m_assTrack) return;
        UpdatePosIfNeeded();

        // 计算当前时间（毫秒）
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - m_startTime).count();
        
        // 渲染字幕
        ASS_Image* img = ass_render_frame(m_assRenderer, m_assTrack, elapsed, nullptr);

        // 清除设备
        m_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
        m_device->BeginScene();

        if (img) {
            // 锁定纹理
            D3DLOCKED_RECT lockedRect;
            if (SUCCEEDED(m_texture->LockRect(0, &lockedRect, nullptr, D3DLOCK_DISCARD))) {
                // 清空纹理
                memset(lockedRect.pBits, 0, lockedRect.Pitch * m_windowHeight);

                // 渲染每个字幕图像
                while (img) {
                    if (img->w > 0 && img->h > 0) {
                        RenderAssImage(img, lockedRect);
                    }
                    img = img->next;
                }

                m_texture->UnlockRect(0);

                // 使用Sprite渲染纹理
                m_sprite->Begin(D3DXSPRITE_ALPHABLEND);
                D3DXVECTOR3 pos(0, 0, 0);
                m_sprite->Draw(m_texture, nullptr, nullptr, &pos, D3DCOLOR_ARGB(255, 255, 255, 255));
                m_sprite->End();
            }
        }

        m_device->EndScene();
        m_device->Present(nullptr, nullptr, nullptr, nullptr);
    }

    void RenderAssImage(ASS_Image* img, const D3DLOCKED_RECT& lockedRect) {
        unsigned char* dst = (unsigned char*)lockedRect.pBits;
        unsigned char* src = img->bitmap;

        unsigned char r = (img->color >> 24) & 0xFF;
        unsigned char g = (img->color >> 16) & 0xFF;
        unsigned char b = (img->color >> 8) & 0xFF;
        unsigned char a = 255 - (img->color & 0xFF);

        for (int y = 0; y < img->h; y++) {
            if (img->dst_y + y >= m_windowHeight) break;
            if (img->dst_y + y < 0) continue;

            for (int x = 0; x < img->w; x++) {
                if (img->dst_x + x >= m_windowWidth) break;
                if (img->dst_x + x < 0) continue;

                unsigned char alpha = (src[y * img->stride + x] * a) / 255;
                if (alpha > 0) {
                    int dstOffset = ((img->dst_y + y) * lockedRect.Pitch) +
                        ((img->dst_x + x) * 4);

                    dst[dstOffset + 0] = b;     // B
                    dst[dstOffset + 1] = g;     // G
                    dst[dstOffset + 2] = r;     // R
                    dst[dstOffset + 3] = alpha; // A
                }
            }
        }
    }

    void Cleanup() {
        if (m_sprite) { m_sprite->Release(); m_sprite = nullptr; }
        if (m_texture) { m_texture->Release(); m_texture = nullptr; }
        if (m_device) { m_device->Release(); m_device = nullptr; }
        if (m_d3d) { m_d3d->Release(); m_d3d = nullptr; }

        if (m_assTrack) { ass_free_track(m_assTrack); m_assTrack = nullptr; }
        if (m_assRenderer) { ass_renderer_done(m_assRenderer); m_assRenderer = nullptr; }
        if (m_assLibrary) { ass_library_done(m_assLibrary); m_assLibrary = nullptr; }

        if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    }

    static LRESULT CALLBACK SubtitleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        SubtitleRenderer* renderer = nullptr;

        if (msg == WM_NCCREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            renderer = (SubtitleRenderer*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)renderer);
        } else {
            renderer = (SubtitleRenderer*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }

        switch (msg) {
        case WM_PAINT:
            if (renderer) {
                renderer->RenderSubtitle();
                ValidateRect(hwnd, nullptr);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1; // 防止擦除背景
        case WM_WINDOWPOSCHANGED:
            if (renderer) {
                renderer->UpdateWindowPosition();
            }
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // 防止鼠标点击激活
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                // 失去激活时不做任何事
                return 0;
            }
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }

    void SetStartTime() {
        m_startTime = std::chrono::steady_clock::now();
    }
};

// 线程函数
DWORD WINAPI SubtitleThreadFunction(LPVOID lpParam) {
    SubtitleThreadParams* params = (SubtitleThreadParams*)lpParam;

    if (!params || !params->parentHwnd) {
        return 1;
    }

    // 初始化COM
    CoInitialize(nullptr);

    SubtitleRenderer renderer;

    if (!renderer.Initialize(params->parentHwnd, params->fileName)) {
        CoUninitialize();
        return 1;
    }

    // 消息循环
    MSG msg;
    auto lastUpdate = std::chrono::steady_clock::now();
    const auto frameInterval = std::chrono::milliseconds(16); // ~60 FPS

    while (!params->shouldStop) {
        // 处理消息
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                params->shouldStop = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        auto currentTime = std::chrono::steady_clock::now();
        if (currentTime - lastUpdate >= frameInterval) {
            renderer.RenderSubtitle();
            lastUpdate = currentTime;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CoUninitialize();
    return 0;
}

void StartPlaySubtitle(HWND parent, const char* filename) {
    if (!fileop::exists(filename)) {
        return;
    }
    if (g_subtitleThread) {
        StopSubtitle();
    }
    g_subtitleParams.parentHwnd = parent;
    g_subtitleParams.shouldStop = false;
    g_subtitleParams.fileName = filename;
    g_subtitleThread = CreateThread(
        nullptr,
        0,
        SubtitleThreadFunction,
        &g_subtitleParams,
        0,
        nullptr
    );
}

void StopSubtitle() {
    if (g_subtitleThread) {
        g_subtitleParams.shouldStop = true;
        WaitForSingleObject(g_subtitleThread, INFINITE);
        CloseHandle(g_subtitleThread);
        g_subtitleThread = nullptr;
    }
}