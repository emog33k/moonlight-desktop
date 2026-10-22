#include "toolbar_win32.h"
#include "win32canvas.h"

#include <windowsx.h>

#include <string>

namespace LocalRdp {

static const wchar_t* const kClassName = L"LocalRdpToolbar";
static const UINT_PTR kTimer = 1;
static const UINT kTimerMs = 100;
static const ULONGLONG kHideDelayMs = 700;
static const ULONGLONG kShowOnEnterMs = 1500; // after entering full screen, so people see it exists
static const int kEdgeZone = 2;               // px at the top of the screen that reveal it
static const ULONGLONG kEdgeDwellMs = 300;    // ...after resting there this long (not a flick past)

// Only one stream (and so one toolbar) exists at a time; the WinEvent hook
// callback has no user pointer, so it finds the toolbar through this.
static ToolbarWindow* s_Instance = nullptr;

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
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
}

static const wchar_t* const kLabelFont = L"Segoe UI";
static const wchar_t* const kIconFont = L"Segoe MDL2 Assets";
static const float kLabelPx = 12.5f;
static const float kIconPx = 15.0f;

ToolbarWindow::ToolbarWindow(HWND owner, std::function<void(int)> onPress)
    : m_Owner(owner),
      m_OnPress(std::move(onPress)),
      m_Buttons(toolbarButtons())
{
    registerClassOnce();
    m_Hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             kClassName, L"", WS_POPUP,
                             0, 0, 1, 1,
                             owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (m_Hwnd == nullptr) {
        return;
    }
    SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, (LONG_PTR)this);
    SetWindowLongPtrW(m_Hwnd, GWLP_WNDPROC, (LONG_PTR)&ToolbarWindow::windowProc);

    // Follow the stream window live, also while Windows is moving/resizing it
    // (SDL only reports those moves after the drag ends).
    s_Instance = this;
    DWORD ownerThread = GetWindowThreadProcessId(owner, nullptr);
    m_LocationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                                     nullptr, &ToolbarWindow::winEventProc,
                                     GetCurrentProcessId(), ownerThread,
                                     WINEVENT_OUTOFCONTEXT);
    SetTimer(m_Hwnd, kTimer, kTimerMs, nullptr);

    reposition();
    setVisible(true);
}

ToolbarWindow::~ToolbarWindow()
{
    if (m_LocationHook != nullptr) {
        UnhookWinEvent(m_LocationHook);
    }
    if (s_Instance == this) {
        s_Instance = nullptr;
    }
    if (m_Hwnd != nullptr) {
        KillTimer(m_Hwnd, kTimer);
        SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_Hwnd);
    }
}

void CALLBACK ToolbarWindow::winEventProc(HWINEVENTHOOK, DWORD, HWND hwnd,
                                          LONG idObject, LONG, DWORD, DWORD)
{
    if (s_Instance != nullptr && idObject == OBJID_WINDOW && hwnd == s_Instance->m_Owner) {
        s_Instance->reposition();
    }
}

QRect ToolbarWindow::ownerClientScreenRect() const
{
    RECT rc;
    GetClientRect(m_Owner, &rc);
    POINT tl = { rc.left, rc.top };
    ClientToScreen(m_Owner, &tl);
    return QRect(tl.x, tl.y, rc.right - rc.left, rc.bottom - rc.top);
}

QPoint ToolbarWindow::cursorPos() const
{
    if (m_CursorProvider) {
        return m_CursorProvider();
    }
    POINT pt;
    GetCursorPos(&pt);
    return QPoint(pt.x, pt.y);
}

int ToolbarWindow::height() const
{
    return (int)(kToolbarBaseHeight * dpiScaleForWindow(m_Owner) + 0.5);
}

QRect ToolbarWindow::buttonScreenRect(int index) const
{
    if (index < 0 || index >= m_Layout.rects.size() || m_Layout.rects[index].isEmpty()) {
        return QRect();
    }
    return m_Layout.rects[index].translated(m_TopLeft);
}

int ToolbarWindow::buttonAtScreen(QPoint screen) const
{
    if (!m_Shown) {
        return -1;
    }
    return hitTestToolbar(m_Layout, screen - m_TopLeft);
}

void ToolbarWindow::relayout(int width, bool compact)
{
    m_Scale = dpiScaleForWindow(m_Owner);
    m_LayoutForWidth = width;
    m_LayoutCompact = compact;

    // Measure labels with the same font the window draws them in
    HDC screen = GetDC(nullptr);
    {
        Gdiplus::Graphics g(screen);
        Gdiplus::FontFamily family(kLabelFont);
        Gdiplus::Font font(&family, (Gdiplus::REAL)(kLabelPx * m_Scale), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        m_Layout = layoutToolbar(m_Buttons, width, m_Scale, [&](const QString& text) {
            const std::wstring w = text.toStdWString();
            Gdiplus::RectF measured;
            g.MeasureString(w.c_str(), -1, &font, Gdiplus::PointF(0, 0), &measured);
            return (int)(measured.Width + 0.99f);
        }, compact);
    }
    ReleaseDC(nullptr, screen);
}

void ToolbarWindow::reposition()
{
    if (m_Hwnd == nullptr || IsIconic(m_Owner)) {
        return;
    }
    const QRect client = ownerClientScreenRect();
    const bool sizeChanged = client.width() != m_LayoutForWidth || m_Fullscreen != m_LayoutCompact ||
                             dpiScaleForWindow(m_Owner) != m_Scale;
    if (sizeChanged) {
        // Full screen: a compact bar in the middle; in a window: the whole width
        relayout(client.width(), m_Fullscreen);
    }
    m_TopLeft = client.topLeft() + QPoint((client.width() - m_Layout.size.width()) / 2, 0);
    if (sizeChanged) {
        render(); // new size needs a new bitmap; also moves the window
    }
    else {
        SetWindowPos(m_Hwnd, nullptr, m_TopLeft.x(), m_TopLeft.y(), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void ToolbarWindow::setVisible(bool visible)
{
    if (m_Hwnd == nullptr) {
        return;
    }
    // Never show over a minimized window (e.g. a stream that starts minimized)
    m_Visible = visible && !IsIconic(m_Owner);
    applyVisibility();
}

void ToolbarWindow::setFullscreen(bool fullscreen)
{
    if (m_Hwnd == nullptr || fullscreen == m_Fullscreen) {
        return;
    }
    m_Fullscreen = fullscreen;
    if (fullscreen) {
        // Show it for a moment so people know where it went
        m_PointerNear = true;
        m_LastNearTick = GetTickCount64() + (kShowOnEnterMs - kHideDelayMs);
    }
    m_EdgeSince = 0;
    reposition(); // compact in full screen, full width in a window
    render();     // a little translucent in full screen
    applyVisibility();
}

void ToolbarWindow::setKeepVisible(bool keep)
{
    m_KeepVisible = keep;
    if (keep) {
        m_LastNearTick = GetTickCount64();
    }
    applyVisibility();
}

void ToolbarWindow::setActiveButton(int index)
{
    if (index != m_Active) {
        m_Active = index;
        render();
    }
}

void ToolbarWindow::setButtons(const QVector<ToolbarButton>& buttons)
{
    if (buttons == m_Buttons) {
        return;
    }
    m_Buttons = buttons;
    m_Hover = -1;
    m_Pressed = -1;
    m_LayoutForWidth = -1; // lay out again
    reposition();
    render();
}

void ToolbarWindow::setAgentStatus(AgentStatus status)
{
    if (status != m_AgentStatus) {
        m_AgentStatus = status;
        render();
    }
}

void ToolbarWindow::setCursorProvider(std::function<QPoint()> provider)
{
    m_CursorProvider = std::move(provider);
}

void ToolbarWindow::applyVisibility()
{
    if (m_Hwnd == nullptr) {
        return;
    }
    const bool show = m_Visible && (!m_Fullscreen || m_KeepVisible || m_PointerNear || m_Pressed >= 0);
    if (show) {
        reposition();
        if (!m_Shown) {
            ShowWindow(m_Hwnd, SW_SHOWNOACTIVATE);
        }
    }
    else if (m_Shown) {
        ShowWindow(m_Hwnd, SW_HIDE);
        m_Hover = -1;
    }
    m_Shown = show;
}

void ToolbarWindow::onTimer()
{
    if (m_StateProvider) {
        const StreamState state = m_StateProvider();
        setAgentStatus(state.agentStatus);
        setButtons(toolbarButtons(state.displays, state.currentDisplay));
    }
    if (!m_Fullscreen || !m_Visible) {
        return;
    }

    const QPoint cursor = cursorPos();
    const QRect client = ownerClientScreenRect();
    const int zone = (int)(kEdgeZone * m_Scale + 0.5);
    // Only above the bar itself (it sits in the middle), and only after resting
    // there: flicking to a corner of the PC's screen never brings it out.
    const QRect bar(m_TopLeft, m_Layout.size);
    const bool atEdge = cursor.x() >= bar.left() && cursor.x() <= bar.right() &&
                        cursor.y() >= client.top() && cursor.y() < client.top() + zone;
    const bool onStrip = m_Shown && bar.contains(cursor);

    const ULONGLONG now = GetTickCount64();
    if (!atEdge) {
        m_EdgeSince = 0;
    }
    else if (m_EdgeSince == 0) {
        m_EdgeSince = now;
    }
    const bool rested = atEdge && now - m_EdgeSince >= kEdgeDwellMs;
    if (rested || onStrip) {
        m_LastNearTick = now;
    }
    const bool wasNear = m_PointerNear;
    m_PointerNear = rested || onStrip || now < m_LastNearTick || now - m_LastNearTick < kHideDelayMs;
    if (m_PointerNear != wasNear) {
        applyVisibility();
    }
}

void ToolbarWindow::setHover(int index)
{
    if (index != m_Hover) {
        m_Hover = index;
        render();
    }
}

void ToolbarWindow::render()
{
    if (m_Hwnd == nullptr || m_Layout.size.width() <= 0 || m_Layout.size.height() <= 0) {
        return;
    }
    Win32Canvas canvas(m_Layout.size.width(), m_Layout.size.height());
    if (!canvas.isValid()) {
        return;
    }
    Gdiplus::Graphics& g = canvas.graphics();
    const Gdiplus::REAL s = (Gdiplus::REAL)m_Scale;
    const Gdiplus::REAL w = (Gdiplus::REAL)m_Layout.size.width();
    const Gdiplus::REAL h = (Gdiplus::REAL)m_Layout.size.height();

    // Strip with a thin line at the bottom
    {
        Gdiplus::SolidBrush bg(Gdiplus::Color(255, 0x1F, 0x1F, 0x23));
        g.FillRectangle(&bg, 0.0f, 0.0f, w, h);
        Gdiplus::Pen line(Gdiplus::Color(255, 0x34, 0x34, 0x3A), 1.0f);
        g.DrawLine(&line, 0.0f, h - 0.5f, w, h - 0.5f);
    }

    Gdiplus::FontFamily labelFamily(kLabelFont);
    Gdiplus::FontFamily iconFamily(kIconFont);
    const bool haveIcons = iconFamily.GetLastStatus() == Gdiplus::Ok;
    Gdiplus::Font labelFont(&labelFamily, kLabelPx * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font iconFont(haveIcons ? &iconFamily : &labelFamily, kIconPx * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 0xE8, 0xE8, 0xEC));

    Gdiplus::StringFormat centered;
    centered.SetAlignment(Gdiplus::StringAlignmentCenter);
    centered.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    centered.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::StringFormat leftFmt;
    leftFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    leftFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    const Gdiplus::REAL pad = 10 * s, iconW = 16 * s, iconGap = 6 * s;
    for (int i = 0; i < m_Buttons.size() && i < m_Layout.rects.size(); i++) {
        const QRect& r = m_Layout.rects[i];
        if (r.isEmpty()) {
            continue;
        }
        const ToolbarButton& b = m_Buttons[i];
        const Gdiplus::RectF rf((Gdiplus::REAL)r.x(), (Gdiplus::REAL)r.y() + 3 * s,
                                (Gdiplus::REAL)r.width(), (Gdiplus::REAL)r.height() - 6 * s);

        // Background: open menu, pressed, hover (disconnect turns red)
        Gdiplus::Color fill(0, 0, 0, 0);
        const bool isDisconnect = b.kind == ToolbarButton::Kind::Action && b.action == StreamAction::Disconnect;
        if (i == m_Active) {
            fill = Gdiplus::Color(255, 0x2B, 0x5C, 0xB8);
        }
        else if (i == m_Pressed) {
            fill = isDisconnect ? Gdiplus::Color(255, 0x9E, 0x22, 0x16) : Gdiplus::Color(255, 0x48, 0x48, 0x50);
        }
        else if (i == m_Hover) {
            fill = isDisconnect ? Gdiplus::Color(255, 0xC4, 0x2B, 0x1C) : Gdiplus::Color(255, 0x38, 0x38, 0x40);
        }
        if (fill.GetA() != 0) {
            Gdiplus::SolidBrush brush(fill);
            g.FillRectangle(&brush, rf);
        }
        if (b.selected) {
            // The monitor shown now: a blue bar under it
            Gdiplus::SolidBrush bar(Gdiplus::Color(255, 0x4F, 0x9B, 0xFF));
            g.FillRectangle(&bar, rf.X + 4 * s, rf.Y + rf.Height - 3 * s, rf.Width - 8 * s, 3 * s);
        }

        // Icon
        const wchar_t glyph[2] = { b.icon, 0 };
        Gdiplus::RectF iconRect = b.label.isEmpty()
                ? rf
                : Gdiplus::RectF(rf.X + pad, rf.Y, iconW, rf.Height);
        if (haveIcons && b.icon != 0) {
            g.DrawString(glyph, 1, &iconFont, iconRect, &centered, &textBrush);
        }

        // Agent status dot on the PC button
        if (b.kind == ToolbarButton::Kind::Section && b.section == MenuSection::Pc) {
            Gdiplus::Color dot;
            switch (m_AgentStatus) {
            case AgentStatus::Connected: dot = Gdiplus::Color(255, 0x4A, 0xDE, 0x80); break;
            case AgentStatus::Connecting: dot = Gdiplus::Color(255, 0xF5, 0xA5, 0x24); break;
            case AgentStatus::Unknown: dot = Gdiplus::Color(255, 0x80, 0x80, 0x88); break;
            default: dot = Gdiplus::Color(255, 0xEF, 0x44, 0x44); break;
            }
            Gdiplus::SolidBrush dotBrush(dot);
            const Gdiplus::REAL d = 7 * s;
            g.FillEllipse(&dotBrush, iconRect.X + iconRect.Width - d * 0.6f,
                          iconRect.Y + iconRect.Height / 2 + 1 * s, d, d);
        }

        // Label
        if (!b.label.isEmpty()) {
            const std::wstring text = b.label.toStdWString();
            Gdiplus::RectF tr(iconRect.X + iconW + iconGap, rf.Y, rf.X + rf.Width - (iconRect.X + iconW + iconGap), rf.Height);
            g.DrawString(text.c_str(), -1, &labelFont, tr, &leftFmt, &textBrush);
        }
    }

    POINT pos = { m_TopLeft.x(), m_TopLeft.y() };
    canvas.present(m_Hwnd, pos, m_Fullscreen ? 240 : 255);
}

LRESULT CALLBACK ToolbarWindow::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto self = (ToolbarWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self == nullptr) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return self->handleMessage(msg, wParam, lParam);
}

LRESULT ToolbarWindow::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_MOUSEACTIVATE:
        // Keep keyboard focus on the stream
        return MA_NOACTIVATE;

    case WM_TIMER:
        if (wParam == kTimer) {
            onTimer();
            return 0;
        }
        break;

    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;

    case WM_MOUSEMOVE:
    {
        if (m_Hover < 0) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_Hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        const QPoint local(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        setHover(hitTestToolbar(m_Layout, local));
        return 0;
    }

    case WM_MOUSELEAVE:
        setHover(-1);
        return 0;

    case WM_LBUTTONDOWN:
    {
        const QPoint local(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        const int index = hitTestToolbar(m_Layout, local);
        if (index < 0) {
            return 0;
        }
        if (m_Buttons[index].kind == ToolbarButton::Kind::Section) {
            // Dropdowns open on press, like a menu bar
            if (m_OnPress) {
                m_OnPress(index);
            }
        }
        else {
            m_Pressed = index;
            SetCapture(m_Hwnd);
            render();
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (m_Pressed < 0) {
            return 0;
        }
        const int pressed = m_Pressed;
        m_Pressed = -1;
        ReleaseCapture();
        render();
        const QPoint local(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hitTestToolbar(m_Layout, local) == pressed && m_OnPress) {
            m_OnPress(pressed);
        }
        applyVisibility();
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (m_Pressed >= 0 && (HWND)lParam != m_Hwnd) {
            // Capture taken away mid-press (e.g. Alt+Tab): cancel
            m_Pressed = -1;
            render();
            applyVisibility();
        }
        return 0;

    // Other buttons and the wheel do nothing here (and never reach the PC)
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return 0;
    }

    return DefWindowProcW(m_Hwnd, msg, wParam, lParam);
}

}
