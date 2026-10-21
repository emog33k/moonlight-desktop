#include "toast_win32.h"
#include "win32canvas.h"

#include <string>

namespace LocalRdp {

static const wchar_t* const kClassName = L"LocalRdpToast";
static const UINT_PTR kHideTimer = 1;

ToastWindow::ToastWindow(HWND owner)
    : m_Owner(owner)
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &ToastWindow::windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    // WS_EX_TRANSPARENT: clicks go through to the stream
    m_Hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                             kClassName, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
}

ToastWindow::~ToastWindow()
{
    if (m_Hwnd != nullptr) {
        KillTimer(m_Hwnd, kHideTimer);
        DestroyWindow(m_Hwnd);
    }
}

LRESULT CALLBACK ToastWindow::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TIMER && wParam == kHideTimer) {
        KillTimer(hwnd, kHideTimer);
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    if (msg == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ToastWindow::hide()
{
    if (m_Hwnd != nullptr) {
        KillTimer(m_Hwnd, kHideTimer);
        ShowWindow(m_Hwnd, SW_HIDE);
    }
}

void ToastWindow::show(const QString& text, const QRect& anchor, const QRect& area, int durationMs)
{
    if (m_Hwnd == nullptr) {
        return;
    }
    const double scale = dpiScaleForWindow(m_Owner);
    const std::wstring wtext = text.toStdWString();

    // Measure the text first
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, (Gdiplus::REAL)(13 * scale), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::RectF measured;
    {
        Win32Canvas probe(1, 1);
        if (!probe.isValid()) {
            return;
        }
        probe.graphics().MeasureString(wtext.c_str(), -1, &font, Gdiplus::PointF(0, 0), &measured);
    }
    const int padX = (int)(14 * scale), padY = (int)(9 * scale);
    const int w = (int)measured.Width + 2 * padX;
    const int h = (int)measured.Height + 2 * padY;

    Win32Canvas canvas(w, h);
    if (!canvas.isValid()) {
        return;
    }
    Gdiplus::Graphics& g = canvas.graphics();
    Gdiplus::GraphicsPath path;
    const Gdiplus::REAL r = (Gdiplus::REAL)(8 * scale), d = 2 * r;
    path.AddArc(0.5f, 0.5f, d, d, 180, 90);
    path.AddArc(w - d - 0.5f, 0.5f, d, d, 270, 90);
    path.AddArc(w - d - 0.5f, h - d - 0.5f, d, d, 0, 90);
    path.AddArc(0.5f, h - d - 0.5f, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush bg(Gdiplus::Color(235, 0x1B, 0x1B, 0x1E));
    g.FillPath(&bg, &path);
    Gdiplus::SolidBrush fg(Gdiplus::Color(255, 0xEE, 0xEE, 0xEE));
    g.DrawString(wtext.c_str(), -1, &font, Gdiplus::PointF((Gdiplus::REAL)padX, (Gdiplus::REAL)padY), &fg);

    // Next to the button, on the side facing into the window, kept inside it
    int x = anchor.center().x() < area.center().x() ? anchor.right() + 1 + padX : anchor.left() - padX - w;
    int y = anchor.center().y() - h / 2;
    x = qBound(area.left(), x, qMax(area.left(), area.left() + area.width() - w));
    y = qBound(area.top(), y, qMax(area.top(), area.top() + area.height() - h));

    POINT pos = { x, y };
    canvas.present(m_Hwnd, pos, 255);
    SetWindowPos(m_Hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(m_Hwnd, kHideTimer, (UINT)qMax(500, durationMs), nullptr);
}

}
