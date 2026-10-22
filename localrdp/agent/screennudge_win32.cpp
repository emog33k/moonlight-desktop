#include "screennudge_win32.h"

#include <QTimer>
#include <QVector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace LocalRdp {

static const wchar_t* const kClassName = L"LocalRdpScreenNudge";
static const int kSize = 2;
static const BYTE kAlpha = 3;  // out of 255: not visible, but a change for the compositor
static const int kShowMs = 100;

static BOOL CALLBACK addMonitor(HMONITOR, HDC, LPRECT rect, LPARAM param)
{
    ((QVector<RECT>*)param)->append(*rect);
    return TRUE;
}

void nudgeScreens()
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }

    QVector<RECT> monitors;
    EnumDisplayMonitors(nullptr, nullptr, &addMonitor, (LPARAM)&monitors);

    QVector<HWND> windows;
    for (const RECT& m : monitors) {
        // Click-through, never activated, not in the taskbar
        HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                    kClassName, L"", WS_POPUP, m.left, m.top, kSize, kSize,
                                    nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (hwnd == nullptr) {
            continue;
        }
        SetLayeredWindowAttributes(hwnd, 0, kAlpha, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
        windows << hwnd;
    }
    QTimer::singleShot(kShowMs, [windows]() {
        for (HWND hwnd : windows) {
            DestroyWindow(hwnd);
        }
    });
}

}
