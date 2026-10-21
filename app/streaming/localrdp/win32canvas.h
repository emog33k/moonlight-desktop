#pragma once

// Off-screen 32-bit canvas for per-pixel-alpha (layered) popup windows.
// Draw with GDI+ through graphics(), then present() it with UpdateLayeredWindow.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
// GDI+ headers use min/max, which Qt disables as macros (NOMINMAX).
namespace Gdiplus {
using std::min;
using std::max;
}
#include <gdiplus.h>

namespace LocalRdp {

class Win32Canvas
{
public:
    Win32Canvas(int width, int height);
    ~Win32Canvas();

    Win32Canvas(const Win32Canvas&) = delete;
    Win32Canvas& operator=(const Win32Canvas&) = delete;

    bool isValid() const { return m_Graphics != nullptr; }
    int width() const { return m_Width; }
    int height() const { return m_Height; }

    // Cleared to fully transparent on creation.
    Gdiplus::Graphics& graphics() { return *m_Graphics; }

    // Shows the canvas as the window's content at topLeft (screen coordinates),
    // multiplied by alpha (255 = opaque).
    bool present(HWND hwnd, POINT topLeft, BYTE alpha);

private:
    int m_Width;
    int m_Height;
    HDC m_Dc = nullptr;
    HBITMAP m_Bitmap = nullptr;
    HGDIOBJ m_OldBitmap = nullptr;
    void* m_Bits = nullptr;
    Gdiplus::Bitmap* m_GdipBitmap = nullptr;
    Gdiplus::Graphics* m_Graphics = nullptr;
};

// Keeps GDI+ initialized while alive. Create one before any Win32Canvas.
class GdiplusSession
{
public:
    GdiplusSession();
    ~GdiplusSession();

private:
    ULONG_PTR m_Token = 0;
};

// DPI scale of a window (1.0 at 96 DPI).
double dpiScaleForWindow(HWND hwnd);

}
