#pragma once
// D3D11 rendering core: flip-model swapchain, a fullscreen triangle that samples
// the captured desktop texture with a zoom transform, plus an optional contrast
// adaptive sharpen (CAS) pass. Zoom is done in UV space - NOT by moving the
// viewport (that would shrink the image instead of magnifying it).

#include "Common.h"
#include "Math2D.h"

namespace aj {

static const char* kQuadShader = R"HLSL(
cbuffer SceneCB : register(b0)
{
    float2 g_CenterUV;   // camera center in source UV space [0,1]
    float  g_Scale;      // magnification (>= 1)
    float  g_Sharpness;  // CAS strength
    float2 g_StepUV;     // uv step of ONE output pixel expressed in source uv
    float  g_Sharpen;    // 0 / 1 toggle
    float  g_Pad;
};

Texture2D    g_Source  : register(t0);
SamplerState g_Sampler : register(s0);

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

// Fullscreen triangle generated from SV_VertexID - no vertex buffer needed.
VSOut VSMain(uint vid : SV_VertexID)
{
    float2 p = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
    VSOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);      // (-1,-1) (3,-1) (-1,3)
    o.uv  = float2(p.x, 1.0 - p.y);               // top-left origin
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    // Zoom: sample a smaller source region and stretch it over the whole target.
    float2 uv = (i.uv - 0.5) / g_Scale + g_CenterUV;
    float3 color = g_Source.Sample(g_Sampler, uv).rgb;

    if (g_Sharpen > 0.5)
    {
        // Contrast Adaptive Sharpening (AMD FSR CAS, 4-neighbour kernel).
        float3 s0 = g_Source.Sample(g_Sampler, uv + float2(0.0, -g_StepUV.y)).rgb;
        float3 s1 = g_Source.Sample(g_Sampler, uv + float2(-g_StepUV.x, 0.0)).rgb;
        float3 s2 = g_Source.Sample(g_Sampler, uv + float2( g_StepUV.x, 0.0)).rgb;
        float3 s3 = g_Source.Sample(g_Sampler, uv + float2(0.0,  g_StepUV.y)).rgb;

        float3 mn = min(color, min(min(s0, s1), min(s2, s3)));
        float3 mx = max(color, max(max(s0, s1), max(s2, s3)));
        float3 amp = saturate(min(mn, 2.0 - mx) / max(mx, 1e-5));
        float3 w = -g_Sharpness * sqrt(amp);
        color = saturate((s0 * w + s1 * w + color + s2 * w + s3 * w) / (1.0 + 4.0 * w));
    }
    return float4(color, 1.0);
}
)HLSL";

// Must match the HLSL cbuffer layout (32 bytes).
struct SceneCB {
    float centerUV[2];
    float scale;
    float sharpness;
    float stepUV[2];
    float sharpen;
    float pad;
};

class Renderer {
public:
    bool Init(HWND hwnd) {
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // required for DXGI DD + D2D
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, _countof(levels), D3D11_SDK_VERSION,
            &m_device, &got, &m_context);
        if (FAILED(hr)) { Logf("[renderer] D3D11CreateDevice failed 0x%08X", hr); return false; }

        if (!CreatePipeline()) return false;
        if (!CreateSwapChain(hwnd)) return false;
        return true;
    }

    ID3D11Device* Device() const { return m_device.Get(); }
    ID3D11DeviceContext* Context() const { return m_context.Get(); }
    IDXGISwapChain1* SwapChain() const { return m_swap.Get(); }
    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }

    bool SetSourceTexture(ID3D11Texture2D* tex) {
        m_srv.Reset();
        HRESULT hr = m_device->CreateShaderResourceView(tex, nullptr, &m_srv);
        if (FAILED(hr)) { Logf("[renderer] CreateSRV failed 0x%08X", hr); return false; }
        return true;
    }

    void Resize(UINT w, UINT h) {
        if (!m_swap || w == 0 || h == 0) return;
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_rtv.Reset();
        HRESULT hr = m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) { Logf("[renderer] ResizeBuffers failed 0x%08X", hr); return; }
        CreateRTV();
        UpdateViewport(w, h);
    }

    void UpdateCB(const Camera2D& cam, bool sharpen, float sharpness, UINT outW, UINT outH) {
        SceneCB cb{};
        cb.centerUV[0] = cam.cx / cam.srcW;
        cb.centerUV[1] = cam.cy / cam.srcH;
        cb.scale = cam.scale;
        cb.sharpness = sharpness;
        cb.stepUV[0] = 1.0f / ((float)outW * cam.scale);
        cb.stepUV[1] = 1.0f / ((float)outH * cam.scale);
        cb.sharpen = sharpen ? 1.0f : 0.0f;

        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(m_context->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, &cb, sizeof(cb));
            m_context->Unmap(m_cb.Get(), 0);
        }
    }

    void RenderImage() {
        if (!m_rtv || !m_srv) return;
        const float clear[4] = { 0.f, 0.f, 0.f, 1.f };
        m_context->ClearRenderTargetView(m_rtv.Get(), clear);
        m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
        m_context->RSSetViewports(1, &m_vp);
        m_context->IASetInputLayout(nullptr);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vs.Get(), nullptr, 0);
        m_context->PSSetShader(m_ps.Get(), nullptr, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
        m_context->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
        m_context->PSSetShaderResources(0, 1, m_srv.GetAddressOf());
        m_context->PSSetSamplers(0, 1, m_smp.GetAddressOf());
        m_context->Draw(3, 0);

        // Unbind so the D2D pass can use the same surface safely, and order our
        // D3D work before D2D's (both share the same D3D device).
        ID3D11ShaderResourceView* nullSrv = nullptr;
        m_context->PSSetShaderResources(0, 1, &nullSrv);
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_context->Flush();
    }

    void Present() {
        if (m_swap) m_swap->Present(0, 0); // unthrottled: our FrameLimiter paces us
    }

private:
    bool CreatePipeline() {
        const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
        ComPtr<ID3DBlob> vsBlob, psBlob, err;

        HRESULT hr = D3DCompile(kQuadShader, strlen(kQuadShader), "quad.hlsl", nullptr, nullptr,
                                "VSMain", "vs_5_0", flags, 0, &vsBlob, &err);
        if (FAILED(hr)) { LogCompileErr(err, "VS"); return false; }

        hr = D3DCompile(kQuadShader, strlen(kQuadShader), "quad.hlsl", nullptr, nullptr,
                        "PSMain", "ps_5_0", flags, 0, &psBlob, &err);
        if (FAILED(hr)) { LogCompileErr(err, "PS"); return false; }

        m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs);
        m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ps);

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(SceneCB);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        m_device->CreateBuffer(&bd, nullptr, &m_cb);

        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.ComparisonFunc = D3D11_COMPARISON_NEVER;
        smp.MinLOD = 0.0f;
        smp.MaxLOD = D3D11_FLOAT32_MAX;
        m_device->CreateSamplerState(&smp, &m_smp);
        return true;
    }

    bool CreateSwapChain(HWND hwnd) {
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&factory))) {
            Log("[renderer] CreateDXGIFactory2 failed");
            return false;
        }
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = m_width;
        sd.Height = m_height;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.Stereo = FALSE;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        sd.Flags = 0;

        HRESULT hr = factory->CreateSwapChainForHwnd(
            m_device.Get(), hwnd, &sd, nullptr, nullptr, &m_swap);
        if (FAILED(hr)) { Logf("[renderer] CreateSwapChainForHwnd failed 0x%08X", hr); return false; }

        ComPtr<IDXGISwapChain2> sc2;
        if (SUCCEEDED(m_swap->QueryInterface(IID_PPV_ARGS(&sc2))))
            sc2->SetMaximumFrameLatency(1);
        CreateRTV();
        UpdateViewport(m_width, m_height);
        return true;
    }

    void CreateRTV() {
        ComPtr<ID3D11Texture2D> bb;
        if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(&bb)))) return;
        m_device->CreateRenderTargetView(bb.Get(), nullptr, &m_rtv);
    }

    void UpdateViewport(UINT w, UINT h) {
        m_width = w; m_height = h;
        m_vp = D3D11_VIEWPORT{};
        m_vp.TopLeftX = 0.f;
        m_vp.TopLeftY = 0.f;
        m_vp.Width = (float)w;
        m_vp.Height = (float)h;
        m_vp.MinDepth = 0.f;
        m_vp.MaxDepth = 1.f;
    }

    static void LogCompileErr(ID3DBlob* err, const char* stage) {
        if (err) Logf("[renderer] %s compile error: %s", stage, (const char*)err->GetBufferPointer());
        else Logf("[renderer] %s compile error (no blob)", stage);
    }

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain1> m_swap;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11Buffer> m_cb;
    ComPtr<ID3D11SamplerState> m_smp;
    ComPtr<ID3D11ShaderResourceView> m_srv;

    UINT m_width = 8;
    UINT m_height = 8;
    D3D11_VIEWPORT m_vp{};
};

} // namespace aj
