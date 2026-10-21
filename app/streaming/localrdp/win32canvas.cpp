#include "win32canvas.h"

namespace LocalRdp {

Win32Canvas::Win32Canvas(int width, int height)
    : m_Width(std::max(width, 1)),
      m_Height(std::max(height, 1))
{
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = m_Width;
    bmi.bmiHeader.biHeight = -m_Height; // top-down rows
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    m_Dc = CreateCompatibleDC(nullptr);
    if (m_Dc == nullptr) {
        return;
    }
    m_Bitmap = CreateDIBSection(m_Dc, &bmi, DIB_RGB_COLORS, &m_Bits, nullptr, 0);
    if (m_Bitmap == nullptr || m_Bits == nullptr) {
        return;
    }
    m_OldBitmap = SelectObject(m_Dc, m_Bitmap);
    ZeroMemory(m_Bits, (size_t)m_Width * m_Height * 4);

    // Premultiplied BGRA in memory is exactly what UpdateLayeredWindow expects.
    m_GdipBitmap = new Gdiplus::Bitmap(m_Width, m_Height, m_Width * 4,
                                       PixelFormat32bppPARGB, (BYTE*)m_Bits);
    m_Graphics = new Gdiplus::Graphics(m_GdipBitmap);
    m_Graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    m_Graphics->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    m_Graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
}

Win32Canvas::~Win32Canvas()
{
    delete m_Graphics;
    delete m_GdipBitmap;
    if (m_Dc != nullptr) {
        if (m_OldBitmap != nullptr) {
            SelectObject(m_Dc, m_OldBitmap);
        }
        DeleteDC(m_Dc);
    }
    if (m_Bitmap != nullptr) {
        DeleteObject(m_Bitmap);
    }
}

bool Win32Canvas::present(HWND hwnd, POINT topLeft, BYTE alpha)
{
    if (!isValid()) {
        return false;
    }
    m_Graphics->Flush(Gdiplus::FlushIntentionSync);

    SIZE size = { m_Width, m_Height };
    POINT src = { 0, 0 };
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = alpha;
    blend.AlphaFormat = AC_SRC_ALPHA;
    return UpdateLayeredWindow(hwnd, nullptr, &topLeft, &size, m_Dc, &src, 0, &blend, ULW_ALPHA) != FALSE;
}

GdiplusSession::GdiplusSession()
{
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&m_Token, &input, nullptr);
}

GdiplusSession::~GdiplusSession()
{
    if (m_Token != 0) {
        Gdiplus::GdiplusShutdown(m_Token);
    }
}

double dpiScaleForWindow(HWND hwnd)
{
    // GetDpiForWindow exists on Windows 10 1607+; look it up so older systems still run.
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn getDpiForWindow =
        (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");

    UINT dpi = 0;
    if (getDpiForWindow != nullptr && hwnd != nullptr) {
        dpi = getDpiForWindow(hwnd);
    }
    if (dpi == 0) {
        HDC screen = GetDC(nullptr);
        dpi = (UINT)GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
    }
    return dpi > 0 ? dpi / 96.0 : 1.0;
}

}
