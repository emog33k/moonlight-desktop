#include "floatingbutton_win32.h"
#include "win32canvas.h"

#include <QSettings>

#include <windowsx.h>

namespace LocalRdp {

static const wchar_t* const kClassName = L"LocalRdpFloatingButton";
static const int kBaseSize = 40;   // px at 96 DPI
static const int kBaseMargin = 8;  // px at 96 DPI
static const BYTE kIdleAlpha = 128;
static const BYTE kActiveAlpha = 255;

// Only one stream (and so one button) exists at a time; the WinEvent hook
// callback has no user pointer, so it finds the button through this.
static FloatingButtonWindow* s_Instance = nullptr;

static void registerClassOnce()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW; // replaced per window below
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
}

FloatingButtonWindow::FloatingButtonWindow(HWND owner, std::function<void()> onClick)
    : m_Owner(owner),
      m_OnClick(std::move(onClick))
{
    QSettings settings;
    m_Placement = loadPlacement(settings);

    registerClassOnce();
    m_Hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             kClassName, L"", WS_POPUP,
                             0, 0, 1, 1,
                             owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (m_Hwnd == nullptr) {
        return;
    }
    SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, (LONG_PTR)this);
    SetWindowLongPtrW(m_Hwnd, GWLP_WNDPROC, (LONG_PTR)&FloatingButtonWindow::windowProc);

    // Follow the stream window live, also while Windows is moving/resizing it
    // (SDL only reports those moves after the drag ends).
    s_Instance = this;
    DWORD ownerThread = GetWindowThreadProcessId(owner, nullptr);
    m_LocationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                                     nullptr, &FloatingButtonWindow::winEventProc,
                                     GetCurrentProcessId(), ownerThread,
                                     WINEVENT_OUTOFCONTEXT);

    reposition();
    setVisible(!IsIconic(owner));
}

FloatingButtonWindow::~FloatingButtonWindow()
{
    if (m_LocationHook != nullptr) {
        UnhookWinEvent(m_LocationHook);
    }
    if (s_Instance == this) {
        s_Instance = nullptr;
    }
    if (m_Hwnd != nullptr) {
        SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_Hwnd);
    }
}

void CALLBACK FloatingButtonWindow::winEventProc(HWINEVENTHOOK, DWORD, HWND hwnd,
                                                 LONG idObject, LONG, DWORD, DWORD)
{
    if (s_Instance != nullptr && idObject == OBJID_WINDOW && hwnd == s_Instance->m_Owner &&
            !s_Instance->m_Dragging) {
        s_Instance->reposition();
    }
}

QRect FloatingButtonWindow::ownerClientScreenRect() const
{
    RECT rc;
    GetClientRect(m_Owner, &rc);
    POINT tl = { rc.left, rc.top };
    ClientToScreen(m_Owner, &tl);
    return QRect(tl.x, tl.y, rc.right - rc.left, rc.bottom - rc.top);
}

int FloatingButtonWindow::buttonSize() const
{
    return (int)(kBaseSize * dpiScaleForWindow(m_Owner) + 0.5);
}

int FloatingButtonWindow::margin() const
{
    return (int)(kBaseMargin * dpiScaleForWindow(m_Owner) + 0.5);
}

QRect FloatingButtonWindow::screenRect() const
{
    return QRect(m_TopLeft, QSize(m_Size, m_Size));
}

void FloatingButtonWindow::reposition()
{
    if (m_Hwnd == nullptr || IsIconic(m_Owner)) {
        return;
    }
    const int size = buttonSize();
    const QPoint topLeft = placeButton(ownerClientScreenRect(), size, margin(), m_Placement);
    if (size != m_Size) {
        m_Size = size;
        m_TopLeft = topLeft;
        render(); // new size needs a new bitmap
    }
    else {
        moveTo(topLeft);
    }
}

void FloatingButtonWindow::moveTo(QPoint topLeft)
{
    m_TopLeft = topLeft;
    SetWindowPos(m_Hwnd, nullptr, topLeft.x(), topLeft.y(), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FloatingButtonWindow::setVisible(bool visible)
{
    if (m_Hwnd == nullptr) {
        return;
    }
    m_Visible = visible;
    if (visible) {
        reposition();
        ShowWindow(m_Hwnd, SW_SHOWNOACTIVATE);
    }
    else {
        ShowWindow(m_Hwnd, SW_HIDE);
    }
}

void FloatingButtonWindow::render()
{
    if (m_Size <= 0) {
        return;
    }
    Win32Canvas canvas(m_Size, m_Size);
    if (!canvas.isValid()) {
        return;
    }
    Gdiplus::Graphics& g = canvas.graphics();
    const Gdiplus::REAL s = (Gdiplus::REAL)m_Size;
    const Gdiplus::REAL inset = s * 0.06f;
    const Gdiplus::REAL d = s - 2 * inset;

    // Soft shadow, then the blue disc
    Gdiplus::SolidBrush shadow(Gdiplus::Color(70, 0, 0, 0));
    g.FillEllipse(&shadow, inset, inset + s * 0.04f, d, d);
    Gdiplus::SolidBrush disc(m_Pressed ? Gdiplus::Color(255, 0x24, 0x63, 0xC8)
                                       : Gdiplus::Color(255, 0x2F, 0x7C, 0xF6));
    g.FillEllipse(&disc, inset, inset, d, d);

    // Three white bars ("hamburger" icon)
    Gdiplus::Pen bar(Gdiplus::Color(255, 255, 255, 255), s * 0.07f);
    bar.SetStartCap(Gdiplus::LineCapRound);
    bar.SetEndCap(Gdiplus::LineCapRound);
    const Gdiplus::REAL cx = s / 2, cy = s / 2 - s * 0.01f;
    const Gdiplus::REAL half = s * 0.2f, gap = s * 0.13f;
    for (int i = -1; i <= 1; i++) {
        g.DrawLine(&bar, cx - half, cy + i * gap, cx + half, cy + i * gap);
    }

    const BYTE alpha = (m_Hover || m_Pressed || m_Dragging) ? kActiveAlpha : kIdleAlpha;
    POINT pos = { m_TopLeft.x(), m_TopLeft.y() };
    canvas.present(m_Hwnd, pos, alpha);
}

LRESULT CALLBACK FloatingButtonWindow::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto self = (FloatingButtonWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self == nullptr) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return self->handleMessage(msg, wParam, lParam);
}

QPoint FloatingButtonWindow::screenPointFromLParam(LPARAM lParam) const
{
    // Mouse message coordinates are relative to this window's client area
    // (also while captured and outside it).
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    ClientToScreen(m_Hwnd, &pt);
    return QPoint(pt.x, pt.y);
}

LRESULT FloatingButtonWindow::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_MOUSEACTIVATE:
        // Keep keyboard focus on the stream
        return MA_NOACTIVATE;

    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;

    case WM_MOUSEMOVE:
    {
        if (!m_Hover) {
            m_Hover = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_Hwnd, 0 };
            TrackMouseEvent(&tme);
            render();
        }
        if (m_Pressed) {
            const QPoint cursor = screenPointFromLParam(lParam);
            if (!m_Dragging && !isClick(m_PressScreenPos, cursor)) {
                m_Dragging = true;
            }
            if (m_Dragging) {
                // Keep the button inside the stream window while dragging
                const QRect client = ownerClientScreenRect();
                QPoint tl = cursor - m_PressOffset;
                tl.setX(std::max(client.left(), std::min(tl.x(), client.left() + client.width() - m_Size)));
                tl.setY(std::max(client.top(), std::min(tl.y(), client.top() + client.height() - m_Size)));
                moveTo(tl);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_Hover = false;
        render();
        return 0;

    case WM_LBUTTONDOWN:
    {
        m_PressScreenPos = screenPointFromLParam(lParam);
        m_PressOffset = m_PressScreenPos - m_TopLeft;
        m_Pressed = true;
        m_Dragging = false;
        SetCapture(m_Hwnd);
        render();
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (!m_Pressed) {
            return 0;
        }
        const bool wasDragging = m_Dragging;
        m_Pressed = false;
        m_Dragging = false;
        ReleaseCapture();

        if (wasDragging) {
            const QPoint center = m_TopLeft + QPoint(m_Size / 2, m_Size / 2);
            m_Placement = snapToNearestEdge(ownerClientScreenRect(), center, m_Size);
            QSettings settings;
            savePlacement(settings, m_Placement);
            reposition();
            render();
        }
        else {
            render();
            if (m_OnClick) {
                m_OnClick();
            }
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (m_Pressed && (HWND)lParam != m_Hwnd) {
            // Capture taken away mid-press (e.g. Alt+Tab): cancel, keep the spot
            m_Pressed = false;
            m_Dragging = false;
            reposition();
            render();
        }
        return 0;

    case WM_RBUTTONUP:
        // Right-click also opens the menu, a common expectation for small buttons
        if (m_OnClick) {
            m_OnClick();
        }
        return 0;
    }

    return DefWindowProcW(m_Hwnd, msg, wParam, lParam);
}

}
