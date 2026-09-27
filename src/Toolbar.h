#pragma once
// Modern floating tool palette (Direct2D).
//
// Look: a bottom-center rounded "pill", translucent dark (acrylic-like), thin
// hairline border, a soft drop shadow, crisp vector icons, hover + active
// states, and circular colour swatches. Zero external assets - all icons are
// drawn with D2D primitives. It is drawn as part of the Annotator's BeginDraw /
// EndDraw pass and hit-tested by App with the same physical-pixel layout.

#include "Common.h"
#include "Tools.h"

namespace aj {

// ---- ordered palette (App maps keys / clicks through these) ----
inline Tool TbToolAt(int i) {
    switch (i) {
        case 0: return Tool::Pen;
        case 1: return Tool::Line;
        case 2: return Tool::Arrow;
        case 3: return Tool::Rect;
        case 4: return Tool::Ellipse;
        case 5: return Tool::Highlight;
        case 6: return Tool::Text;
        default: return Tool::Pen;
    }
}

inline int TbToolIndex(Tool t) {
    switch (t) {
        case Tool::Pen:       return 0;
        case Tool::Line:      return 1;
        case Tool::Arrow:     return 2;
        case Tool::Rect:      return 3;
        case Tool::Ellipse:   return 4;
        case Tool::Highlight: return 5;
        case Tool::Text:      return 6;
        default:              return 0;
    }
}

inline D2D1_COLOR_F TbPaletteColor(int i) {
    switch (i) {
        case 0:  return D2D1::ColorF(0.94f, 0.27f, 0.27f); // red
        case 1:  return D2D1::ColorF(0.26f, 0.86f, 0.38f); // green
        case 2:  return D2D1::ColorF(0.29f, 0.62f, 1.00f); // blue
        case 3:  return D2D1::ColorF(1.00f, 0.90f, 0.22f); // yellow
        case 4:  return D2D1::ColorF(1.00f, 0.58f, 0.16f); // orange
        case 5:  return D2D1::ColorF(0.93f, 0.94f, 0.97f); // white
        case 6:  return D2D1::ColorF(0.10f, 0.10f, 0.12f); // black
        default: return D2D1::ColorF(1, 0, 0);
    }
}

enum class TbKind { Tool, Color, Divider, Undo, Clear, Exit };

struct TbButton {
    D2D1_RECT_F rect{};
    TbKind kind = TbKind::Tool;
    Tool tool = Tool::Pen;
    D2D1_COLOR_F color{};
    int idx = -1;   // palette index for tools/colours
};

class Toolbar {
public:
    void SetViewport(float w, float h) {
        if (w != m_w || h != m_h) { m_w = w; m_h = h; m_needLayout = true; }
    }
    void SetActive(int toolIdx, int colorIdx, int hoverIdx) {
        m_activeTool = toolIdx; m_activeColor = colorIdx; m_hover = hoverIdx;
    }

    int HitTest(float x, float y) {
        Layout();
        for (int i = 0; i < (int)m_btns.size(); ++i) {
            const TbButton& b = m_btns[i];
            if (b.kind == TbKind::Divider) continue;
            if (x >= b.rect.left && x <= b.rect.right && y >= b.rect.top && y <= b.rect.bottom)
                return i;
        }
        return -1;
    }
    const TbButton& Button(int i) const { return m_btns[i]; }

    void Draw(ID2D1DeviceContext* c, float w, float h) {
        m_w = w; m_h = h;
        Layout();
        EnsureRes(c);
        if (!m_bg) return;

        // soft shadow (cheap: offset dark rounded rect)
        c->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(m_bar.left - 1, m_bar.top + 3, m_bar.right + 1, m_bar.bottom + 6),
                              m_radius + 2, m_radius + 2), m_shadow.Get());
        // panel
        c->FillRoundedRectangle(D2D1::RoundedRect(m_bar, m_radius, m_radius), m_bg.Get());
        c->DrawRoundedRectangle(D2D1::RoundedRect(m_bar, m_radius, m_radius), m_border.Get(), 1.0f);

        for (int i = 0; i < (int)m_btns.size(); ++i) {
            const TbButton& b = m_btns[i];
            if (b.kind == TbKind::Divider) {
                c->DrawLine(D2D1::Point2F(b.rect.left, b.rect.top),
                            D2D1::Point2F(b.rect.left, b.rect.bottom), m_divider.Get(), 1.0f);
                continue;
            }
            const bool active = (b.kind == TbKind::Tool && b.idx == m_activeTool) ||
                                (b.kind == TbKind::Color && b.idx == m_activeColor);
            if (active && b.kind != TbKind::Color) {
                c->FillRoundedRectangle(D2D1::RoundedRect(b.rect, 9.0f, 9.0f), m_active.Get());
            } else if (i == m_hover) {
                c->FillRoundedRectangle(D2D1::RoundedRect(b.rect, 9.0f, 9.0f), m_hover.Get());
            }
            DrawIcon(c, b);
        }
    }

private:
    void Layout() {
        if (!m_needLayout) return;
        m_needLayout = false;
        m_btns.clear();

        const float btn = 38.0f, gap = 5.0f, padX = 12.0f, groupGap = 16.0f;
        const float bh = 54.0f;

        struct Item { TbKind k; Tool t; D2D1_COLOR_F col; int idx; float x; };
        std::vector<Item> items;
        float rel = 0.0f;
        auto push = [&](TbKind k, Tool t, D2D1_COLOR_F col, int idx) {
            items.push_back({ k, t, col, idx, rel });
            rel += (k == TbKind::Divider) ? (groupGap + 1.0f + groupGap) : (btn + gap);
        };
        for (int i = 0; i < kToolCount; ++i) push(TbKind::Tool, TbToolAt(i), {}, i);
        push(TbKind::Divider, Tool::Pen, {}, -1);
        for (int i = 0; i < 7; ++i) push(TbKind::Color, Tool::Pen, TbPaletteColor(i), i);
        push(TbKind::Divider, Tool::Pen, {}, -1);
        push(TbKind::Undo, Tool::Pen, {}, -1);
        push(TbKind::Clear, Tool::Pen, {}, -1);
        push(TbKind::Exit, Tool::Pen, {}, -1);
        rel -= gap; // no trailing gap

        const float total = padX * 2.0f + rel;
        const float left = (m_w - total) * 0.5f;
        const float top = m_h - bh - 24.0f;
        m_bar = D2D1::RectF(left, top, left + total, top + bh);
        const float btnTop = top + (bh - btn) * 0.5f;

        for (const Item& it : items) {
            TbButton b;
            b.kind = it.k;
            b.tool = it.t;
            b.color = it.col;
            b.idx = it.idx;
            if (it.k == TbKind::Divider) {
                const float dx = left + padX + it.x + groupGap;
                b.rect = D2D1::RectF(dx, top + 13.0f, dx, top + bh - 13.0f);
            } else {
                const float bx = left + padX + it.x;
                b.rect = D2D1::RectF(bx, btnTop, bx + btn, btnTop + btn);
            }
            m_btns.push_back(b);
        }
    }

    void EnsureRes(ID2D1DeviceContext* c) {
        if (m_bg) return;
        c->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.10f, 0.12f, 0.94f), &m_bg);
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.10f), &m_border);
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.12f), &m_divider);
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.13f), &m_hover);
        c->CreateSolidColorBrush(D2D1::ColorF(0.34f, 0.55f, 1.00f, 0.95f), &m_active);
        c->CreateSolidColorBrush(D2D1::ColorF(0.93f, 0.94f, 0.97f, 1.00f), &m_icon);
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.30f), &m_swatchBorder);
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), &m_activeRing);
        c->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.45f), &m_shadow);
        c->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &m_swatch);
    }

    static ComPtr<ID2D1PathGeometry> MakeArc(ID2D1Factory* f, D2D1_POINT_2F ctr, float r,
                                             float startDeg, float sweepDeg) {
        ComPtr<ID2D1PathGeometry> g;
        if (FAILED(f->CreatePathGeometry(&g))) return nullptr;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(g->Open(&sink))) return nullptr;
        const float PI = 3.14159265f;
        const float a0 = startDeg * PI / 180.0f;
        const float a1 = (startDeg + sweepDeg) * PI / 180.0f;
        sink->BeginFigure(D2D1::Point2F(ctr.x + r * cosf(a0), ctr.y + r * sinf(a0)),
                          D2D1_FIGURE_BEGIN_HOLLOW);
        D2D1_ARC_SEGMENT seg{};
        seg.point = D2D1::Point2F(ctr.x + r * cosf(a1), ctr.y + r * sinf(a1));
        seg.size = D2D1::SizeF(r, r);
        seg.rotationAngle = 0.0f;
        seg.sweepDirection = (sweepDeg >= 0.0f) ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                                                : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
        seg.arcSize = (fabsf(sweepDeg) > 180.0f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
        sink->AddArc(seg);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        return g;
    }

    void DrawIcon(ID2D1DeviceContext* c, const TbButton& b) {
        const float cx = (b.rect.left + b.rect.right) * 0.5f;
        const float cy = (b.rect.top + b.rect.bottom) * 0.5f;
        const float s = 7.5f;
        auto P = [&](float dx, float dy) { return D2D1::Point2F(cx + dx, cy + dy); };
        ID2D1Brush* st = m_icon.Get();
        const float w = 1.7f;

        switch (b.kind) {
            case TbKind::Tool:
                switch (b.tool) {
                    case Tool::Pen:
                        c->DrawLine(P(-s, s), P(s * 0.7f, -s * 0.7f), st, 2.2f);
                        c->DrawLine(P(-s, s), P(-s * 0.45f, s * 0.1f), st, 2.2f);
                        break;
                    case Tool::Line:
                        c->DrawLine(P(-s, s), P(s, -s), st, w);
                        break;
                    case Tool::Arrow:
                        c->DrawLine(P(-s, s), P(s, -s), st, w);
                        c->DrawLine(P(s, -s), P(s * 0.30f, -s), st, w);
                        c->DrawLine(P(s, -s), P(s, -s * 0.30f), st, w);
                        break;
                    case Tool::Rect:
                        c->DrawRectangle(D2D1::RectF(cx - s, cy - s, cx + s, cy + s), st, w);
                        break;
                    case Tool::Ellipse:
                        c->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s, s * 0.82f), st, w);
                        break;
                    case Tool::Highlight:
                        c->DrawLine(P(-s, s * 0.6f), P(s, -s * 0.6f), st, 4.5f);
                        break;
                    case Tool::Text:
                        c->DrawLine(P(-s * 0.85f, -s * 0.55f), P(s * 0.85f, -s * 0.55f), st, w);
                        c->DrawLine(P(0, -s * 0.55f), P(0, s), st, w);
                        break;
                }
                break;

            case TbKind::Color: {
                const D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(cx, cy), s + 1.5f, s + 1.5f);
                m_swatch->SetColor(b.color);
                c->FillEllipse(e, m_swatch.Get());
                c->DrawEllipse(e, m_swatchBorder.Get(), 1.0f);
                if (b.idx == m_activeColor)
                    c->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s + 4.5f, s + 4.5f),
                                   m_activeRing.Get(), 2.0f);
                break;
            }
            case TbKind::Undo:
                c->DrawLine(P(-s, -3.0f), P(s, -3.0f), st, w);
                c->DrawLine(P(-s, -3.0f), P(-s * 0.30f, -3.0f - s), st, w);
                c->DrawLine(P(-s, -3.0f), P(-s * 0.30f, -3.0f + s * 0.5f), st, w);
                c->DrawLine(P(-s, -3.0f), P(-s, s), st, w);
                c->DrawLine(P(-s, s), P(s * 0.55f, s), st, w);
                break;
            case TbKind::Clear:
                c->DrawLine(P(-s * 0.8f, -s * 0.8f), P(s * 0.8f, s * 0.8f), st, 1.9f);
                c->DrawLine(P(s * 0.8f, -s * 0.8f), P(-s * 0.8f, s * 0.8f), st, 1.9f);
                break;
            case TbKind::Exit: {
                ComPtr<ID2D1PathGeometry> arc =
                    MakeArc(c->GetFactory(), D2D1::Point2F(cx, cy), s * 0.92f, 300.0f, 300.0f);
                if (arc) c->DrawGeometry(arc.Get(), st, w);
                c->DrawLine(P(0, -s - 2.0f), P(0, 0), st, w);
                break;
            }
            default: break;
        }
    }

    ID2D1SolidColorBrush* brush() { return nullptr; }

    float m_w = 1920.f, m_h = 1080.f;
    bool m_needLayout = true;
    int m_activeTool = 0, m_activeColor = 0, m_hover = -1;
    float m_radius = 16.0f;
    D2D1_RECT_F m_bar{};
    std::vector<TbButton> m_btns;

    ComPtr<ID2D1SolidColorBrush> m_bg, m_border, m_divider, m_hover, m_active,
        m_icon, m_swatchBorder, m_activeRing, m_shadow, m_swatch;
};

} // namespace aj
