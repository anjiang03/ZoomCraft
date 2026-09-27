#pragma once
// Frame-rate governor: 60 / 90 / 120 Hz locked, or Unlimited.
// Hybrid sleep + spin wait on QueryPerformanceCounter for low jitter.
// "Unlimited" does not wait at all -> render as fast as the GPU/loop allows.

#include "Common.h"

namespace aj {

enum class RefreshCap : int {
    Hz60 = 0,
    Hz90,
    Hz120,
    Unlimited,
    Count
};

inline const wchar_t* RefreshCapName(RefreshCap c) {
    switch (c) {
        case RefreshCap::Hz60:      return L"60 FPS";
        case RefreshCap::Hz90:      return L"90 FPS";
        case RefreshCap::Hz120:     return L"120 FPS";
        case RefreshCap::Unlimited: return L"Unlimited";
        default:                    return L"?";
    }
}

class FrameLimiter {
public:
    void Init() {
        QueryPerformanceFrequency(&m_freq);
        QueryPerformanceCounter(&m_next);
        timeBeginPeriod(1); // 1ms Sleep granularity
    }
    void Shutdown() {
        timeEndPeriod(1);
    }

    void SetCap(RefreshCap c) {
        m_cap = c;
        Reset();
    }
    RefreshCap Cap() const { return m_cap; }
    void NextCap() {
        int n = (static_cast<int>(m_cap) + 1) % static_cast<int>(RefreshCap::Count);
        SetCap(static_cast<RefreshCap>(n));
    }

    double Interval() const {
        switch (m_cap) {
            case RefreshCap::Hz60:  return 1.0 / 60.0;
            case RefreshCap::Hz90:  return 1.0 / 90.0;
            case RefreshCap::Hz120: return 1.0 / 120.0;
            default:                return 0.0; // unlimited
        }
    }

    void Reset() { QueryPerformanceCounter(&m_next); }

    // Block until the next frame slot. No-op in Unlimited mode.
    void Wait() {
        const double iv = Interval();
        if (iv <= 0.0) return;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double remain = static_cast<double>(m_next.QuadPart - now.QuadPart) / m_freq.QuadPart;

        if (remain > 0.0) {
            // Sleep the bulk, spin the last ~1.2 ms for precision.
            if (remain > 0.0015) {
                DWORD ms = static_cast<DWORD>((remain - 0.0012) * 1000.0);
                if (ms > 0) Sleep(ms);
            }
            do {
                QueryPerformanceCounter(&now);
                remain = static_cast<double>(m_next.QuadPart - now.QuadPart) / m_freq.QuadPart;
            } while (remain > 0.0);
        }

        // Schedule next slot; if we fell behind by more than a frame, resync.
        m_next.QuadPart += static_cast<LONGLONG>(iv * m_freq.QuadPart);
        QueryPerformanceCounter(&now);
        if (static_cast<double>(now.QuadPart - m_next.QuadPart) / m_freq.QuadPart > iv) {
            m_next = now;
        }
    }

private:
    RefreshCap m_cap = RefreshCap::Unlimited;
    LARGE_INTEGER m_freq{};
    LARGE_INTEGER m_next{};
};

} // namespace aj
