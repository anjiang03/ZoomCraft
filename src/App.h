#pragma once
// Dual-engine application.
//
//   Zooming  -> MagnifierEngine (live, click-through, real-time desktop zoom)
//   Annotating -> frozen D3D11 frame + CAS sharpen + Direct2D annotation
//
// Interaction (from the agreed spec):
//   Alt+Shift+W      toggle Live zoom (global hook)
//   wheel            smooth zoom while Live
//   left click       (Live) intercept -> freeze frame -> annotate
//   right click/Esc  exit everything -> Idle (0 cost)
//
// Live zoom uses the Magnification API, so input passes through to the app
// underneath (ZBrush / UE / etc.) and there is NO self-capture feedback loop.

#include "Common.h"
#include "Math2D.h"
#include "FrameLimiter.h"
#include "ScreenCapture.h"
#include "Renderer.h"
#include "Annotator.h"
#include "Toolbar.h"
#include "MagnifierEngine.h"
#include <dwmapi.h>

namespace aj {

class App {
public:
    bool Init(HINSTANCE hInst) {
        m_hInst = hInst;
        s_instance = this;
        m_limiter.Init();

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &App::WndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"ZoomCraftOverlay";
        if (!RegisterClassExW(&wc)) { Log("[app] RegisterClassEx failed"); return false; }

        // The D3D11 overlay window (opaque, topmost, fullscreen when shown).
        // NOT layered - flip-model swapchains can't live on a layered HWND.
        m_hwnd = CreateWindowExW(WS_EX_TOPMOST, L"ZoomCraftOverlay", L"ZoomCraft",
                                 WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, hInst, this);
        if (!m_hwnd) { Log("[app] CreateWindowEx failed"); return false; }

        if (!m_renderer.Init(m_hwnd)) return false;
        if (!m_annotator.Init(m_renderer.Device(), m_renderer.SwapChain())) return false;
        if (!m_magnifier.Init(hInst)) return false;

        m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, &App::MouseProc, GetModuleHandleW(nullptr), 0);
        m_keyHook = SetWindowsHookExW(WH_KEYBOARD_LL, &App::KeyProc, GetModuleHandleW(nullptr), 0);
        if (!m_mouseHook || !m_keyHook) { Log("[app] SetWindowsHookEx failed"); return false; }

        QueryPerformanceFrequency(&m_freq);
        Log("[app] ready - Alt+Shift+W to zoom");
        return true;
    }

    int Run() {
        MSG msg{};
        while (msg.message != WM_QUIT) {
            if (m_state == State::Idle) {
                if (GetMessageW(&msg, nullptr, 0, 0) <= 0) break; // blocks (hooks still fire)
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            } else {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) { Shutdown(); return (int)msg.wParam; }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                RenderFrame();
            }
        }
        Shutdown();
        return (int)msg.wParam;
    }

private:
    enum class State { Idle, Zooming, Annotating };

    void Shutdown() {
        if (m_mouseHook) { UnhookWindowsHookEx(m_mouseHook); m_mouseHook = nullptr; }
        if (m_keyHook) { UnhookWindowsHookEx(m_keyHook); m_keyHook = nullptr; }
        m_magnifier.Shutdown();
        m_limiter.Shutdown();
    }

    // ---------------- state transitions ----------------
    void StartLiveZoom() {
        m_annotator.Clear();
        m_source.Reset();
        ShowWindow(m_hwnd, SW_HIDE);              // hide overlay; magnifier shows real desktop

        m_monRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        m_srcW = (UINT)m_monRect.right;
        m_srcH = (UINT)m_monRect.bottom;

        m_camera.srcW = (float)m_srcW;
        m_camera.srcH = (float)m_srcH;
        m_camera.minScale = 1.0f;
        m_camera.maxScale = 5.0f;

        POINT p{};
        GetCursorPos(&p);
        m_camera.scale = 1.0f;                    // spring in from 1.0x
        m_camera.cx = m_camera.tx = (float)p.x;
        m_camera.cy = m_camera.ty = (float)p.y;
        m_camera.targetScale = 2.0f;
        m_camera.vx = m_camera.vy = m_camera.vs = 0.f;

        m_state = State::Zooming;
        m_magnifier.Show(true);
        m_limiter.Reset();
        QueryPerformanceCounter(&m_last);
        SetOsdText(L"Live zoom   |   wheel: zoom   L: annotate   R/Esc: exit");
    }

    void SwitchToAnnotating() {
        if (m_state != State::Zooming) return;

        // Hide magnifier, then let DWM composite it away BEFORE grabbing a frame,
        // otherwise the captured frame would contain the magnifier's own output.
        m_magnifier.Show(false);
        DwmFlush();

        if (!ScreenCapture::CaptureMonitor(m_renderer.Device(), MonitorFromRect(&m_monRect, MONITOR_DEFAULTTONEAREST),
                                           m_source, m_srcW, m_srcH)) {
            Log("[app] capture failed - aborting annotate");
            ExitZoom();
            return;
        }
        m_renderer.SetSourceTexture(m_source.Get());

        // Release D2D target ref -> resize swapchain -> show overlay.
        m_annotator.OnResize();
        SetWindowPos(m_hwnd, HWND_TOPMOST, m_monRect.left, m_monRect.top,
                     (int)m_srcW, (int)m_srcH, SWP_NOACTIVATE);
        m_renderer.Resize(m_srcW, m_srcH);

        // Freeze the camera at the exact view the magnifier was showing.
        m_camera.srcW = (float)m_srcW;
        m_camera.srcH = (float)m_srcH;
        m_camera.tx = m_camera.cx;
        m_camera.ty = m_camera.cy;
        m_camera.tscale = m_camera.scale;
        m_camera.ClampView();

        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);

        m_state = State::Annotating;
        m_strokeActive = false;
        m_limiter.Reset();
        QueryPerformanceCounter(&m_last);
        SetOsdText(L"Frozen   |   draw   C: clear   Ctrl+Z: undo   R/Esc: exit");
    }

    void ExitZoom() {
        m_magnifier.Show(false);
        m_annotator.Clear();
        m_source.Reset();
        ShowWindow(m_hwnd, SW_HIDE);
        m_camera.scale = 1.0f;
        m_camera.targetScale = 2.0f;
        m_state = State::Idle;
        m_limiter.Reset();
        Log("[app] idle");
    }

    // ---------------- per-frame ----------------
    void RenderFrame() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - m_last.QuadPart) / (double)m_freq.QuadPart);
        m_last = now;
        if (dt > 0.1f) dt = 0.1f;

        if (m_state == State::Zooming) {
            POINT p{};
            GetCursorPos(&p);
            m_camera.SetPanTarget((float)p.x, (float)p.y);
            m_camera.Update(dt);
            m_magnifier.SetSource(m_camera.GetSourceRect(m_monRect.right, m_monRect.bottom),
                                  m_camera.scale);
        } else if (m_state == State::Annotating) {
            const UINT w = m_renderer.Width();
            const UINT h = m_renderer.Height();
            m_renderer.UpdateCB(m_camera, m_sharpen, m_sharpness, w, h);
            m_renderer.RenderImage();
            m_osdTimer = (m_osdTimer > dt) ? (m_osdTimer - dt) : 0.0f;
            m_toolbar.SetViewport((float)w, (float)h);
            m_toolbar.SetActive(TbToolIndex(m_annotator.CurrentTool()), m_colorIdx, m_hoverIdx);
            m_annotator.Render(m_camera, (float)w, (float)h, m_osd, m_osdTimer > 0.0f, &m_toolbar);
            m_renderer.Present();
        }

        m_limiter.Wait();
    }

    void UpdateCursorSource() {
        POINT p{};
        GetCursorPos(&p);
        const float sx = (float)(p.x - m_monRect.left);
        const float sy = (float)(p.y - m_monRect.top);
        m_camera.ScreenToSource(sx, sy, (float)m_renderer.Width(), (float)m_renderer.Height(),
                                m_curX, m_curY);
        m_curX = ClampF(m_curX, 0.f, (float)m_srcW);
        m_curY = ClampF(m_curY, 0.f, (float)m_srcH);
    }

    // ---------------- window messages (used while Annotating) ----------------
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        App* self = nullptr;
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(l);
            self = static_cast<App*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = h;
        } else {
            self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (self) return self->HandleMessage(m, w, l);
        return DefWindowProcW(h, m, w, l);
    }

    LRESULT HandleMessage(UINT m, WPARAM w, LPARAM l) {
        switch (m) {
            case WM_MOUSEMOVE: {
                if (m_state == State::Annotating) {
                    POINT p{}; GetCursorPos(&p);
                    const float mx = (float)(p.x - m_monRect.left);
                    const float my = (float)(p.y - m_monRect.top);
                    m_hoverIdx = m_toolbar.HitTest(mx, my);
                    if (m_strokeActive && m_hoverIdx < 0) {
                        UpdateCursorSource();
                        m_annotator.OnMove(m_curX, m_curY);
                    }
                }
                return 0;
            }
            case WM_LBUTTONDOWN:
                if (m_state == State::Annotating) {
                    POINT p{}; GetCursorPos(&p);
                    const float mx = (float)(p.x - m_monRect.left);
                    const float my = (float)(p.y - m_monRect.top);
                    const int idx = m_toolbar.HitTest(mx, my);
                    if (idx >= 0) {
                        OnToolbarClick(idx);
                    } else {
                        UpdateCursorSource();
                        m_annotator.OnDown(m_curX, m_curY);
                        m_strokeActive = true;
                    }
                }
                return 0;
            case WM_LBUTTONUP:
                if (m_strokeActive) { m_annotator.OnUp(); m_strokeActive = false; }
                return 0;
            case WM_KEYDOWN:
                OnKeyDown((int)w);
                return 0;
            case WM_CHAR:
                if (m_annotator.IsEditingText()) {
                    const wchar_t c = (wchar_t)w;
                    if (c == L'\r') { m_annotator.FinishText(); m_strokeActive = false; }
                    else if (c == L'\b') m_annotator.Backspace();
                    else if (c >= 32) m_annotator.AppendChar(c);
                }
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(m_hwnd, m, w, l);
    }

    void OnKeyDown(int vk) {
        if (m_annotator.IsEditingText()) {
            if (vk == VK_ESCAPE) { m_annotator.FinishText(); m_strokeActive = false; }
            return;
        }
        switch (vk) {
            case VK_ESCAPE: ExitZoom(); break;
            case VK_F1:
                m_limiter.NextCap();
                SetOsdText(std::wstring(L"Frame rate: ") + RefreshCapName(m_limiter.Cap()));
                break;
            case VK_F2:
                m_sharpen = !m_sharpen;
                SetOsdText(m_sharpen ? L"Sharpen ON" : L"Sharpen OFF");
                break;
            case 'C': m_annotator.Clear(); break;
            case 'Z': if (GetKeyState(VK_CONTROL) < 0) m_annotator.Undo(); break;
            case '1': m_annotator.SetTool(Tool::Pen);       SetOsdText(L"Tool: Pen"); break;
            case '2': m_annotator.SetTool(Tool::Line);      SetOsdText(L"Tool: Line"); break;
            case '3': m_annotator.SetTool(Tool::Arrow);     SetOsdText(L"Tool: Arrow"); break;
            case '4': m_annotator.SetTool(Tool::Rect);      SetOsdText(L"Tool: Rectangle"); break;
            case '5': m_annotator.SetTool(Tool::Ellipse);   SetOsdText(L"Tool: Ellipse"); break;
            case '6': m_annotator.SetTool(Tool::Highlight); SetOsdText(L"Tool: Highlighter"); break;
            case '7': m_annotator.SetTool(Tool::Text);      SetOsdText(L"Tool: Text"); break;
            case 'R': PickColor(0); break;
            case 'G': PickColor(1); break;
            case 'B': PickColor(2); break;
            case 'Y': PickColor(3); break;
            case 'O': PickColor(4); break;
            case 'W': PickColor(5); break;
            case 'K': PickColor(6); break;
            default: break;
        }
    }

    // ---------------- global low-level hooks ----------------
    static LRESULT CALLBACK MouseProc(int nCode, WPARAM w, LPARAM l) {
        if (nCode >= 0 && s_instance && s_instance->OnMouseHook(w, l)) return 1;
        return CallNextHookEx(nullptr, nCode, w, l);
    }
    static LRESULT CALLBACK KeyProc(int nCode, WPARAM w, LPARAM l) {
        if (nCode >= 0 && s_instance && s_instance->OnKeyHook(w, l)) return 1;
        return CallNextHookEx(nullptr, nCode, w, l);
    }

    bool OnMouseHook(WPARAM w, LPARAM l) {
        if (m_state == State::Zooming) {
            if (w == WM_MOUSEWHEEL) {
                const int delta = GET_WHEEL_DELTA_WPARAM(((MSLLHOOKSTRUCT*)l)->mouseData);
                m_camera.ZoomTo(m_camera.ZoomTarget() * (delta > 0 ? 1.12f : (1.0f / 1.12f)));
                SetOsdZoom();
                return true;                       // swallow: don't zoom the app underneath
            }
            if (w == WM_LBUTTONDOWN) { SwitchToAnnotating(); return true; }
            if (w == WM_RBUTTONDOWN) { ExitZoom(); return true; }
        } else if (m_state == State::Annotating) {
            if (w == WM_RBUTTONDOWN) { ExitZoom(); return true; }
        }
        return false;
    }

    bool OnKeyHook(WPARAM w, LPARAM l) {
        if (w != WM_KEYDOWN && w != WM_SYSKEYDOWN) return false;
        const DWORD vk = ((KBDLLHOOKSTRUCT*)l)->vkCode;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        if (alt && shift && vk == 'W') {
            if (m_state == State::Idle || m_state == State::Annotating) StartLiveZoom();
            return true;                           // swallow, so the CG app never sees Alt+Shift+W
        }
        if (vk == VK_ESCAPE && m_state != State::Idle) { ExitZoom(); return true; }
        if (vk == VK_F1) {                         // cycle fps even during Live (window unfocused)
            m_limiter.NextCap();
            SetOsdText(std::wstring(L"Frame rate: ") + RefreshCapName(m_limiter.Cap()));
            return true;
        }
        return false;
    }

    // ---------------- helpers ----------------
    void PickColor(int idx) {
        m_colorIdx = idx;
        m_annotator.SetColor(TbPaletteColor(idx));
    }
    void OnToolbarClick(int idx) {
        const TbButton& b = m_toolbar.Button(idx);
        switch (b.kind) {
            case TbKind::Tool:  m_annotator.SetTool(b.tool); break;
            case TbKind::Color: PickColor(b.idx); break;
            case TbKind::Undo:  m_annotator.Undo(); break;
            case TbKind::Clear: m_annotator.Clear(); break;
            case TbKind::Exit:  ExitZoom(); break;
            default: break;
        }
    }
    void SetOsdText(std::wstring s) { m_osd = L"  " + std::move(s); m_osdTimer = 1.8f; }
    void SetOsdZoom() {
        wchar_t buf[64];
        swprintf_s(buf, L"  Zoom %.0f%%", m_camera.ZoomTarget() * 100.0f);
        m_osd = buf;
        m_osdTimer = 1.2f;
    }

    static App* s_instance;
    const float m_sharpness = 0.4f;

    HINSTANCE m_hInst = nullptr;
    HWND m_hwnd = nullptr;
    State m_state = State::Idle;

    Renderer m_renderer;
    Annotator m_annotator;
    Toolbar m_toolbar;
    MagnifierEngine m_magnifier;
    Camera2D m_camera;
    FrameLimiter m_limiter;

    HHOOK m_mouseHook = nullptr;
    HHOOK m_keyHook = nullptr;

    ComPtr<ID3D11Texture2D> m_source;
    UINT m_srcW = 0, m_srcH = 0;
    RECT m_monRect{};

    float m_curX = 0.f, m_curY = 0.f;
    bool m_strokeActive = false;
    bool m_sharpen = true;
    int m_hoverIdx = -1;
    int m_colorIdx = 0;

    std::wstring m_osd;
    float m_osdTimer = 0.0f;

    LARGE_INTEGER m_freq{};
    LARGE_INTEGER m_last{};
};

inline App* App::s_instance = nullptr;

} // namespace aj
