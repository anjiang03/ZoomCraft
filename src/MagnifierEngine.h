#pragma once
// Live magnification via the Windows Magnification API.
// This is the "dynamic" engine: it magnifies the REAL desktop in real time,
// is click-through (input passes to the app underneath), and - because DWM
// owns the composition - it NEVER captures itself (no feedback loop).
//
// Downside: we can't inject our own shader here, so the live view uses the
// system's built-in smoothing (no CAS). CAS is applied later, on the frozen
// frame, by the D3D11 renderer.

#include "Common.h"
#include <magnification.h>
#pragma comment(lib, "magnification.lib")

namespace aj {

class MagnifierEngine {
public:
    bool Init(HINSTANCE hInst) {
        if (!MagInitialize()) { Log("[magnifier] MagInitialize failed"); return false; }
        m_ok = true;

        m_w = GetSystemMetrics(SM_CXSCREEN);
        m_h = GetSystemMetrics(SM_CYSCREEN);

        // Topmost, layered, click-through host covering the primary monitor.
        m_host = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
            L"STATIC", nullptr, WS_POPUP,
            0, 0, m_w, m_h,
            nullptr, nullptr, hInst, nullptr);
        if (!m_host) { Log("[magnifier] host CreateWindow failed"); return false; }
        SetLayeredWindowAttributes(m_host, 0, 255, LWA_ALPHA);

        m_mag = CreateWindowW(
            WC_MAGNIFIER, L"ZoomCraftMagnifier", WS_CHILD | WS_VISIBLE,
            0, 0, m_w, m_h, m_host, nullptr, hInst, nullptr);
        if (!m_mag) { Log("[magnifier] magnifier CreateWindow failed"); return false; }

        return true;
    }

    // Feed the current camera: the source rect (screen px) to magnify + the scale.
    void SetSource(const RECT& src, float scale) {
        if (!m_mag) return;
        MAGTRANSFORM t{};
        t.v[0][0] = scale;
        t.v[1][1] = scale;
        t.v[2][2] = 1.0f;
        MagSetWindowTransform(m_mag, &t);
        MagSetWindowSource(m_mag, src);
        InvalidateRect(m_mag, nullptr, FALSE);
    }

    void Show(bool show) {
        if (m_host) ShowWindow(m_host, show ? SW_SHOW : SW_HIDE);
    }

    void Shutdown() {
        if (m_host) { DestroyWindow(m_host); m_host = nullptr; }
        if (m_ok) { MagUninitialize(); m_ok = false; }
    }

private:
    HWND m_host = nullptr;
    HWND m_mag = nullptr;
    LONG m_w = 0, m_h = 0;
    bool m_ok = false;
};

} // namespace aj
