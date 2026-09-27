#pragma once
// One-shot desktop capture of a monitor into an ID3D11Texture2D.
// Primary path: DXGI Desktop Duplication (VRAM->VRAM copy, no CPU readback).
// Fallback: GDI BitBlt (for RDP / adapters where Duplication is unavailable).
//
// IMPORTANT: call this while our overlay window is HIDDEN. We capture once and
// freeze the frame, so there is no self-capture feedback loop.

#include "Common.h"

namespace aj {

class ScreenCapture {
public:
    static bool CaptureMonitor(ID3D11Device* device, HMONITOR mon,
                               ComPtr<ID3D11Texture2D>& outTex, UINT& outW, UINT& outH) {
        if (TryDesktopDuplication(device, mon, outTex, outW, outH)) {
            Log("[capture] DXGI Desktop Duplication");
            return true;
        }
        if (TryGdi(device, mon, outTex, outW, outH)) {
            Log("[capture] GDI BitBlt (fallback)");
            return true;
        }
        Log("[capture] FAILED");
        return false;
    }

private:
    static bool TryDesktopDuplication(ID3D11Device* device, HMONITOR mon,
                                      ComPtr<ID3D11Texture2D>& outTex, UINT& outW, UINT& outH) {
        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) return false;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;

        // Find the output whose monitor matches the requested one. This matters on
        // hybrid/multi-GPU machines where the default adapter is not the right one.
        ComPtr<IDXGIOutput> output;
        for (UINT i = 0; ; ++i) {
            ComPtr<IDXGIOutput> o;
            if (adapter->EnumOutputs(i, &o) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC d{};
            if (SUCCEEDED(o->GetDesc(&d)) && d.Monitor == mon) { output = o; break; }
        }
        if (!output) return false;

        ComPtr<IDXGIOutput1> output1;
        if (FAILED(output->QueryInterface(IID_PPV_ARGS(&output1)))) return false;

        ComPtr<IDXGIOutputDuplication> dupl;
        if (FAILED(output1->DuplicateOutput(device, &dupl))) return false;

        ComPtr<ID3D11DeviceContext> ctx;
        device->GetImmediateContext(&ctx);

        for (int attempt = 0; attempt < 8; ++attempt) {
            DXGI_OUTDUPL_FRAME_INFO info{};
            ComPtr<IDXGIResource> res;
            HRESULT hr = dupl->AcquireNextFrame(500, &info, &res);
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;   // nothing changed yet - retry
            if (FAILED(hr)) return false;

            ComPtr<ID3D11Texture2D> src;
            hr = res->QueryInterface(IID_PPV_ARGS(&src));
            if (FAILED(hr)) { dupl->ReleaseFrame(); return false; }

            D3D11_TEXTURE2D_DESC desc{};
            src->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            desc.ArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;

            ComPtr<ID3D11Texture2D> dst;
            hr = device->CreateTexture2D(&desc, nullptr, &dst);
            if (FAILED(hr)) { dupl->ReleaseFrame(); return false; }

            ctx->CopyResource(dst.Get(), src.Get());
            ctx->Flush();           // finish VRAM->VRAM copy before releasing the frame
            dupl->ReleaseFrame();

            outTex = dst;
            outW = desc.Width;
            outH = desc.Height;
            return true;
        }
        return false;
    }

    static bool TryGdi(ID3D11Device* device, HMONITOR mon,
                       ComPtr<ID3D11Texture2D>& outTex, UINT& outW, UINT& outH) {
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfo(mon, &mi)) return false;
        const int w = mi.rcMonitor.right - mi.rcMonitor.left;
        const int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        if (w <= 0 || h <= 0) return false;

        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;   // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp) { DeleteDC(memDC); ReleaseDC(nullptr, screenDC); return false; }
        HGDIOBJ oldBmp = SelectObject(memDC, bmp);

        BitBlt(memDC, 0, 0, w, h, screenDC, mi.rcMonitor.left, mi.rcMonitor.top, SRCCOPY);
        GdiFlush();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(w);
        desc.Height = static_cast<UINT>(h);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = bits;
        sd.SysMemPitch = static_cast<UINT>(w) * 4;
        sd.SysMemSlicePitch = 0;

        ComPtr<ID3D11Texture2D> tex;
        HRESULT hr = device->CreateTexture2D(&desc, &sd, &tex);

        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);

        if (FAILED(hr)) return false;
        outTex = tex;
        outW = static_cast<UINT>(w);
        outH = static_cast<UINT>(h);
        return true;
    }
};

} // namespace aj
