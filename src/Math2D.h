#pragma once
// Camera math: Unity-style Critically Damped SmoothDamp (the same spring used by
// obs-zoom-to-mouse). Frame-rate independent because it takes delta time.

#include "Common.h"

namespace aj {

inline float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float LerpF(float a, float b, float t) { return a + (b - a) * t; }

// Critically damped spring, Unity semantics. Returns the new value; updates vel.
inline float SmoothDamp(float current, float target, float& velocity,
                        float smoothTime, float dt) {
    if (smoothTime < 1e-4f) smoothTime = 1e-4f;
    if (dt <= 0.0f) return current;

    const float omega = 2.0f / smoothTime;
    const float x = omega * dt;
    const float expTerm = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = current - target;
    const float temp = (velocity + omega * change) * dt;

    velocity = (velocity - omega * temp) * expTerm;
    return target + (change + temp) * expTerm;
}

// A 2D camera over a source image. All positions are in SOURCE PIXELS.
// scale >= 1 (we only magnify). The view is clamped so it never shows outside
// the source image.
struct Camera2D {
    // current
    float cx = 0.f, cy = 0.f, scale = 1.f;
    // targets
    float tx = 0.f, ty = 0.f, tscale = 1.f;
    // velocities
    float vx = 0.f, vy = 0.f, vs = 0.f;
    // source dimensions (pixels)
    float srcW = 1920.f, srcH = 1080.f;
    // bounds
    float minScale = 1.0f, maxScale = 8.0f;

    void Reset(float sx, float sy, float s) {
        cx = tx = sx;
        cy = ty = sy;
        scale = tscale = ClampF(s, minScale, maxScale);
        vx = vy = vs = 0.f;
    }

    void SetPanTarget(float sx, float sy) { tx = sx; ty = sy; }
    void ZoomTo(float s) { tscale = ClampF(s, minScale, maxScale); }
    float ZoomTarget() const { return tscale; }

    void Update(float dt, float smoothPan = 0.11f, float smoothZoom = 0.14f) {
        cx = SmoothDamp(cx, tx, vx, smoothPan, dt);
        cy = SmoothDamp(cy, ty, vy, smoothPan, dt);
        scale = SmoothDamp(scale, tscale, vs, smoothZoom, dt);
        ClampView();
    }

    void ClampView() {
        scale = ClampF(scale, minScale, maxScale);
        const float hw = srcW / (2.f * scale);
        const float hh = srcH / (2.f * scale);
        cx = ClampF(cx, hw, srcW - hw);
        cy = ClampF(cy, hh, srcH - hh);
        tx = ClampF(tx, hw, srcW - hw);
        ty = ClampF(ty, hh, srcH - hh);
    }

    bool Settled(float eps = 0.5f) const {
        return std::fabs(cx - tx) < eps && std::fabs(cy - ty) < eps &&
               std::fabs(scale - tscale) < 1e-3f &&
               std::fabs(vx) < eps && std::fabs(vy) < eps &&
               std::fabs(vs) < 1e-3f;
    }

    // map a source pixel to output UV [0,1]
    void SourceToOutputUV(float sx, float sy, float& u, float& v) const {
        u = ((sx / srcW) - (cx / srcW)) * scale + 0.5f;
        v = ((sy / srcH) - (cy / srcH)) * scale + 0.5f;
    }

    // inverse: a screen pixel -> the source pixel underneath it (for annotation).
    void ScreenToSource(float sx, float sy, float outW, float outH,
                        float& srcX, float& srcY) const {
        const float su = ((sx / outW) - 0.5f) / scale + (cx / srcW);
        const float sv = ((sy / outH) - 0.5f) / scale + (cy / srcH);
        srcX = su * srcW;
        srcY = sv * srcH;
    }

    // The source rectangle currently visible (used to drive the Magnification API).
    RECT GetSourceRect(LONG w, LONG h) const {
        const float s = (scale < 1.0001f) ? 1.0001f : scale;
        const float sw = (float)w / s;
        const float sh = (float)h / s;
        float left = cx - sw * 0.5f;
        float top = cy - sh * 0.5f;
        left = ClampF(left, 0.f, (float)w - sw);
        top = ClampF(top, 0.f, (float)h - sh);
        RECT r{};
        r.left = (LONG)left;
        r.top = (LONG)top;
        r.right = (LONG)(left + sw);
        r.bottom = (LONG)(top + sh);
        return r;
    }
};

} // namespace aj
