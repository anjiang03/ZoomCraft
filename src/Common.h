#pragma once
// ZoomCraft - shared includes & helpers.
// Pure GPU pipeline: DXGI Desktop Duplication (capture once) -> D3D11 (zoom+CAS) -> D2D (annotation).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <timeapi.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")

using Microsoft::WRL::ComPtr;

namespace aj {

inline void Log(const char* msg) {
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

inline void Logf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log(buf);
}

} // namespace aj
