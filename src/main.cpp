// ZoomCraft - entry point.
#include "App.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // Per-monitor DPI aware so cursor/screen coordinates are physical pixels
    // (matters on multi-monitor setups with mixed scaling).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    aj::App app;
    if (!app.Init(hInstance)) {
        MessageBoxW(nullptr, L"ZoomCraft failed to initialise.\n"
                             L"See debug output for details.", L"ZoomCraft",
                    MB_ICONERROR | MB_OK);
        return 1;
    }
    return app.Run();
}
