#pragma once
// Annotation layer: Direct2D + DirectWrite vector drawing composited on top of
// the D3D11 back buffer (both share the same D3D device, created from its
// IDXGIDevice). Annotation geometry is stored in SOURCE-IMAGE pixels so it
// stays glued to the magnified content; it is transformed to screen space each
// frame with the current camera.

#include "Common.h"
#include "Math2D.h"
#include "Tools.h"
#include "Toolbar.h"

namespace aj {

struct Annotation {
    Tool tool = Tool::Pen;
    std::vector<D2D1_POINT_2F> pts;   // source-pixel space
    D2D1_COLOR_F color{ 1.f, 0.f, 0.f, 1.f };
    float width = 3.0f;               // stroke width in screen px
    std::wstring text;                // Tool::Text
};

class Annotator {
public:
    bool Init(ID3D11Device* device, IDXGISwapChain1* swap) {
        m_swap = swap;

        D2D1_FACTORY_OPTIONS fo{};
        ID2D1Factory1* fac = nullptr;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     __uuidof(ID2D1Factory1), &fo, (void**)&fac))) {
            Log("[annotator] D2D1CreateFactory failed");
            return false;
        }
        m_factory.Attach(fac);
        ComPtr<IDXGIDevice> dxgiDev;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDev)))) return false;
        if (FAILED(m_factory->CreateDevice(dxgiDev.Get(), &m_d2dDevice))) return false;
        if (FAILED(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2d))) return false;

        IDWriteFactory* dw = nullptr;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       (IUnknown**)&dw))) {
            Log("[annotator] DWriteCreateFactory failed");
            return false;
        }
        m_dwrite.Attach(dw);
        m_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"en-us", &m_osdFormat);
        m_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 26.0f, L"en-us", &m_textFormat);

        m_d2d->CreateSolidColorBrush(D2D1::ColorF(1, 0, 0, 1), &m_brush);
        m_d2d->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.55f), &m_bgBrush);
        return true;
    }

    // Release the D2D target BEFORE the swapchain resizes (it holds a ref to the
    // back buffer, which would block ResizeBuffers).
    void OnResize() { m_target.Reset(); }

    void SetTool(Tool t) { m_tool = t; }
    void SetColor(const D2D1_COLOR_F& c) { m_color = c; }
    Tool CurrentTool() const { return m_tool; }
    D2D1_COLOR_F CurrentColor() const { return m_color; }
    bool HasContent() const { return !m_anns.empty(); }

    void Clear() { m_anns.clear(); m_editing = -1; }
    void Undo() { if (!m_anns.empty()) m_anns.pop_back(); m_editing = -1; }

    // ---- input (source-pixel coordinates) ----
    void OnDown(float sx, float sy) {
        if (m_tool == Tool::Text) { BeginText(sx, sy); return; }
        m_anns.emplace_back();
        Annotation& a = m_anns.back();
        a.tool = m_tool;
        a.color = m_color;
        a.width = (m_tool == Tool::Highlight) ? 20.0f : 3.0f;
        a.pts.push_back(D2D1::Point2F(sx, sy));
    }
    void OnMove(float sx, float sy) {
        if (m_anns.empty()) return;
        Annotation& a = m_anns.back();
        switch (a.tool) {
            case Tool::Pen:
            case Tool::Highlight:
                a.pts.push_back(D2D1::Point2F(sx, sy));
                break;
            case Tool::Line:
            case Tool::Arrow:
            case Tool::Rect:
            case Tool::Ellipse:
                if (a.pts.size() < 2) a.pts.push_back(D2D1::Point2F(sx, sy));
                else a.pts[1] = D2D1::Point2F(sx, sy);
                break;
            default: break;
        }
    }
    void OnUp() {}

    void BeginText(float sx, float sy) {
        m_anns.emplace_back();
        Annotation& a = m_anns.back();
        a.tool = Tool::Text;
        a.color = m_color;
        a.pts.push_back(D2D1::Point2F(sx, sy));
        m_editing = (int)m_anns.size() - 1;
    }
    bool IsEditingText() const { return m_editing >= 0 && m_editing < (int)m_anns.size(); }
    void AppendChar(wchar_t c) { if (IsEditingText()) m_anns[m_editing].text.push_back(c); }
    void Backspace() {
        if (IsEditingText() && !m_anns[m_editing].text.empty())
            m_anns[m_editing].text.pop_back();
    }
    void FinishText() { m_editing = -1; }

    // ---- render ----
    void Render(const Camera2D& cam, float outW, float outH,
                const std::wstring& osd, bool osdVisible, Toolbar* toolbar = nullptr) {
        if (!m_d2d) return;
        const bool wantBar = (toolbar != nullptr);
        if (m_anns.empty() && !osdVisible && !wantBar) return;

        CreateTarget();
        if (!m_target) return;

        CacheView(cam, outW, outH);
        m_d2d->SetTarget(m_target.Get());
        m_d2d->BeginDraw();

        for (size_t i = 0; i < m_anns.size(); ++i)
            DrawAnnotation(m_anns[i], (int)i == m_editing);

        if (osdVisible && !osd.empty()) DrawOsd(osd);
        if (toolbar) toolbar->Draw(m_d2d.Get(), outW, outH);

        m_d2d->EndDraw();
    }

private:
    D2D1_POINT_2F ToScreen(float sx, float sy) const {
        float u = ((sx / v_srcW) - (v_cx / v_srcW)) * v_scale + 0.5f;
        float v = ((sy / v_srcH) - (v_cy / v_srcH)) * v_scale + 0.5f;
        return D2D1::Point2F(u * v_outW, v * v_outH);
    }

    void CacheView(const Camera2D& cam, float outW, float outH) {
        v_srcW = cam.srcW; v_srcH = cam.srcH;
        v_cx = cam.cx; v_cy = cam.cy; v_scale = cam.scale;
        v_outW = outW; v_outH = outH;
    }

    void CreateTarget() {
        if (m_target) return;
        ComPtr<IDXGISurface> surf;
        if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(&surf)))) return;
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        HRESULT hr = m_d2d->CreateBitmapFromDxgiSurface(surf.Get(), &props, &m_target);
        if (FAILED(hr)) Logf("[annotator] CreateBitmapFromDxgiSurface failed 0x%08X", hr);
    }

    void DrawAnnotation(const Annotation& a, bool editing) {
        m_brush->SetColor(a.color);
        m_brush->SetOpacity(1.0f);

        switch (a.tool) {
            case Tool::Pen: {
                for (size_t i = 1; i < a.pts.size(); ++i)
                    m_d2d->DrawLine(ToScreen(a.pts[i - 1].x, a.pts[i - 1].y),
                                    ToScreen(a.pts[i].x, a.pts[i].y),
                                    m_brush.Get(), a.width);
                break;
            }
            case Tool::Highlight: {
                m_brush->SetOpacity(0.35f);
                for (size_t i = 1; i < a.pts.size(); ++i)
                    m_d2d->DrawLine(ToScreen(a.pts[i - 1].x, a.pts[i - 1].y),
                                    ToScreen(a.pts[i].x, a.pts[i].y),
                                    m_brush.Get(), a.width);
                m_brush->SetOpacity(1.0f);
                break;
            }
            case Tool::Line: {
                if (a.pts.size() < 2) break;
                m_d2d->DrawLine(ToScreen(a.pts[0].x, a.pts[0].y),
                                ToScreen(a.pts[1].x, a.pts[1].y),
                                m_brush.Get(), a.width);
                break;
            }
            case Tool::Arrow: {
                if (a.pts.size() < 2) break;
                const D2D1_POINT_2F p0 = ToScreen(a.pts[0].x, a.pts[0].y);
                const D2D1_POINT_2F p1 = ToScreen(a.pts[1].x, a.pts[1].y);
                m_d2d->DrawLine(p0, p1, m_brush.Get(), a.width);

                float dx = p1.x - p0.x, dy = p1.y - p0.y;
                float len = sqrtf(dx * dx + dy * dy);
                if (len > 1.0f) {
                    dx /= len; dy /= len;
                    const float head = 14.0f + a.width * 2.5f;
                    const float ca = cosf(0.5f), sa = sinf(0.5f);
                    const D2D1_POINT_2F h1 = D2D1::Point2F(
                        p1.x - head * (dx * ca - dy * sa), p1.y - head * (dx * sa + dy * ca));
                    const D2D1_POINT_2F h2 = D2D1::Point2F(
                        p1.x - head * (dx * ca + dy * sa), p1.y - head * (-dx * sa + dy * ca));
                    m_d2d->DrawLine(p1, h1, m_brush.Get(), a.width);
                    m_d2d->DrawLine(p1, h2, m_brush.Get(), a.width);
                }
                break;
            }
            case Tool::Rect: {
                if (a.pts.size() < 2) break;
                const D2D1_POINT_2F p0 = ToScreen(a.pts[0].x, a.pts[0].y);
                const D2D1_POINT_2F p1 = ToScreen(a.pts[1].x, a.pts[1].y);
                const D2D1_RECT_F r = D2D1::RectF(
                    (std::min)(p0.x, p1.x), (std::min)(p0.y, p1.y),
                    (std::max)(p0.x, p1.x), (std::max)(p0.y, p1.y));
                m_d2d->DrawRectangle(r, m_brush.Get(), a.width);
                break;
            }
            case Tool::Ellipse: {
                if (a.pts.size() < 2) break;
                const D2D1_POINT_2F p0 = ToScreen(a.pts[0].x, a.pts[0].y);
                const D2D1_POINT_2F p1 = ToScreen(a.pts[1].x, a.pts[1].y);
                const D2D1_ELLIPSE e = D2D1::Ellipse(
                    D2D1::Point2F((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f),
                    fabsf(p1.x - p0.x) * 0.5f, fabsf(p1.y - p0.y) * 0.5f);
                m_d2d->DrawEllipse(e, m_brush.Get(), a.width);
                break;
            }
            case Tool::Text: {
                std::wstring t = a.text;
                if (editing) t += L"|";   // caret
                if (t.empty()) break;
                const D2D1_POINT_2F p = ToScreen(a.pts[0].x, a.pts[0].y);
                const D2D1_RECT_F r = D2D1::RectF(p.x, p.y, p.x + 4000.0f, p.y + 4000.0f);
                m_d2d->DrawTextW(t.c_str(), (UINT32)t.size(), m_textFormat.Get(),
                                 r, m_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
                break;
            }
        }
    }

    void DrawOsd(const std::wstring& osd) {
        const D2D1_RECT_F bg = D2D1::RectF(24, 24, 24 + osd.size() * 15.0f + 60, 78);
        m_d2d->FillRectangle(bg, m_bgBrush.Get());
        m_brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
        m_brush->SetOpacity(1.0f);
        const D2D1_RECT_F tr = D2D1::RectF(40, 30, 4000, 120);
        m_d2d->DrawTextW(osd.c_str(), (UINT32)osd.size(), m_osdFormat.Get(), tr, m_brush.Get());
    }

    IDXGISwapChain1* m_swap = nullptr;   // non-owning

    ComPtr<ID2D1Factory1> m_factory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<ID2D1DeviceContext> m_d2d;
    ComPtr<IDWriteFactory> m_dwrite;
    ComPtr<IDWriteTextFormat> m_osdFormat;
    ComPtr<IDWriteTextFormat> m_textFormat;
    ComPtr<ID2D1SolidColorBrush> m_brush;
    ComPtr<ID2D1SolidColorBrush> m_bgBrush;
    ComPtr<ID2D1Bitmap1> m_target;

    std::vector<Annotation> m_anns;
    int m_editing = -1;
    Tool m_tool = Tool::Pen;
    D2D1_COLOR_F m_color{ 1.f, 0.f, 0.f, 1.f };

    // cached view for the current frame
    float v_srcW = 1920.f, v_srcH = 1080.f;
    float v_cx = 960.f, v_cy = 540.f, v_scale = 1.f;
    float v_outW = 1920.f, v_outH = 1080.f;
};

} // namespace aj
