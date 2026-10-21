#include "streammenu_win32.h"
#include "win32canvas.h"

#include <windowsx.h>

#include <string>

namespace LocalRdp {

static const wchar_t* const kClassName = L"LocalRdpStreamMenu";

static void registerClassOnce()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW; // replaced per window
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
}

static std::wstring wide(const QString& s)
{
    return s.toStdWString();
}

StreamMenuWindow::StreamMenuWindow(HWND owner,
                                   std::function<void(StreamAction)> onAction,
                                   std::function<void()> onDismissed)
    : m_Owner(owner),
      m_OnAction(std::move(onAction)),
      m_OnDismissed(std::move(onDismissed))
{
    registerClassOnce();
    m_Hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             kClassName, L"", WS_POPUP,
                             0, 0, 1, 1,
                             owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (m_Hwnd != nullptr) {
        SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, (LONG_PTR)this);
        SetWindowLongPtrW(m_Hwnd, GWLP_WNDPROC, (LONG_PTR)&StreamMenuWindow::windowProc);
    }
}

StreamMenuWindow::~StreamMenuWindow()
{
    close();
    if (m_Hwnd != nullptr) {
        SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_Hwnd);
    }
}

void StreamMenuWindow::show(const QRect& buttonScreenRect, Edge buttonEdge, const QVector<MenuItem>& items)
{
    if (m_Hwnd == nullptr) {
        return;
    }
    m_Items = items;
    m_Scale = dpiScaleForWindow(m_Owner);
    m_Layout = layoutMenu(m_Items, m_Scale);
    m_Highlight = -1;
    m_PressedIndex = -1;

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(m_Owner, MONITOR_DEFAULTTONEAREST), &mi);
    const QRect work(mi.rcWork.left, mi.rcWork.top,
                     mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top);
    m_TopLeft = placeMenu(buttonScreenRect, buttonEdge, m_Layout.size, work,
                          (int)(6 * m_Scale + 0.5));

    render();
    SetWindowPos(m_Hwnd, HWND_TOP, m_TopLeft.x(), m_TopLeft.y(), 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_Open = true;

    // Clicks outside the panel come to us so we can close instead of
    // letting them reach the stream.
    SetCapture(m_Hwnd);
}

void StreamMenuWindow::close()
{
    if (!m_Open) {
        return;
    }
    // Mark closed first: ReleaseCapture() sends WM_CAPTURECHANGED synchronously.
    m_Open = false;
    if (GetCapture() == m_Hwnd) {
        ReleaseCapture();
    }
    ShowWindow(m_Hwnd, SW_HIDE);
}

void StreamMenuWindow::dismiss()
{
    if (!m_Open) {
        return;
    }
    close();
    if (m_OnDismissed) {
        m_OnDismissed();
    }
}

void StreamMenuWindow::activate(int index)
{
    if (index < 0 || index >= m_Items.size() || !m_Items[index].action) {
        return;
    }
    const StreamAction action = *m_Items[index].action;
    close();
    if (m_OnAction) {
        m_OnAction(action);
    }
}

void StreamMenuWindow::moveHighlight(int dir)
{
    if (!m_Open) {
        return;
    }
    setHighlight(nextActionable(m_Items, m_Highlight, dir));
}

void StreamMenuWindow::activateHighlighted()
{
    if (m_Open && m_Highlight >= 0) {
        activate(m_Highlight);
    }
}

void StreamMenuWindow::setHighlight(int index)
{
    if (index != m_Highlight) {
        m_Highlight = index;
        render();
    }
}

QPoint StreamMenuWindow::localPointFromLParam(LPARAM lParam) const
{
    // Client coordinates of this popup are relative to its top-left corner.
    return QPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
}

bool StreamMenuWindow::containsLocal(QPoint local) const
{
    return QRect(QPoint(0, 0), m_Layout.size).contains(local);
}

void StreamMenuWindow::render()
{
    Win32Canvas canvas(m_Layout.size.width(), m_Layout.size.height());
    if (!canvas.isValid()) {
        return;
    }
    Gdiplus::Graphics& g = canvas.graphics();
    const Gdiplus::REAL s = (Gdiplus::REAL)m_Scale;

    auto roundRect = [](Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y,
                        Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r) {
        const Gdiplus::REAL d = 2 * r;
        path.AddArc(x, y, d, d, 180, 90);
        path.AddArc(x + w - d, y, d, d, 270, 90);
        path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        path.AddArc(x, y + h - d, d, d, 90, 90);
        path.CloseFigure();
    };

    // Panel
    {
        Gdiplus::GraphicsPath panel;
        roundRect(panel, 0.5f, 0.5f, m_Layout.size.width() - 1.0f, m_Layout.size.height() - 1.0f, 8 * s);
        Gdiplus::SolidBrush bg(Gdiplus::Color(245, 0x1B, 0x1B, 0x1E));
        g.FillPath(&bg, &panel);
        Gdiplus::Pen border(Gdiplus::Color(255, 0x3A, 0x3A, 0x3E), 1.0f);
        g.DrawPath(&border, &panel);
    }

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font labelFont(&family, 13 * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font headerFont(&family, 11 * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font shortcutFont(&family, 11 * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 0xEE, 0xEE, 0xEE));
    Gdiplus::SolidBrush dimBrush(Gdiplus::Color(255, 0x9A, 0x9A, 0xA0));
    Gdiplus::SolidBrush shortcutBrush(Gdiplus::Color(255, 0x8A, 0x8A, 0x90));

    Gdiplus::StringFormat leftFmt;
    leftFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    leftFmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    leftFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::StringFormat rightFmt;
    rightFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    rightFmt.SetAlignment(Gdiplus::StringAlignmentFar);
    rightFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    const Gdiplus::REAL markerW = 28 * s;
    const Gdiplus::REAL textPad = 10 * s;

    for (int i = 0; i < m_Items.size() && i < m_Layout.itemRects.size(); i++) {
        const MenuItem& item = m_Items[i];
        const QRect& r = m_Layout.itemRects[i];
        const Gdiplus::RectF rf((Gdiplus::REAL)r.x(), (Gdiplus::REAL)r.y(),
                                (Gdiplus::REAL)r.width(), (Gdiplus::REAL)r.height());

        switch (item.kind) {
        case MenuItemKind::Header:
        {
            const std::wstring text = wide(item.label.toUpper());
            Gdiplus::RectF tr(rf.X + textPad, rf.Y + 2 * s, rf.Width - 2 * textPad, rf.Height);
            g.DrawString(text.c_str(), -1, &headerFont, tr, &leftFmt, &dimBrush);
            break;
        }
        case MenuItemKind::Separator:
        {
            Gdiplus::Pen line(Gdiplus::Color(255, 0x3A, 0x3A, 0x3E), 1.0f);
            const Gdiplus::REAL y = rf.Y + rf.Height / 2;
            g.DrawLine(&line, rf.X + 8 * s, y, rf.X + rf.Width - 8 * s, y);
            break;
        }
        default:
        {
            if (i == m_Highlight) {
                Gdiplus::GraphicsPath hl;
                roundRect(hl, rf.X, rf.Y + 1, rf.Width, rf.Height - 2, 5 * s);
                Gdiplus::SolidBrush hlBrush(Gdiplus::Color(0x30, 255, 255, 255));
                g.FillPath(&hlBrush, &hl);
            }

            // Marker column: dot for the selected radio, tick for a checked box
            const Gdiplus::REAL mx = rf.X + markerW / 2, my = rf.Y + rf.Height / 2;
            if (item.kind == MenuItemKind::Radio) {
                Gdiplus::Pen ring(Gdiplus::Color(255, 0x6A, 0x6A, 0x70), 1.2f * s);
                g.DrawEllipse(&ring, mx - 5 * s, my - 5 * s, 10 * s, 10 * s);
                if (item.checked) {
                    Gdiplus::SolidBrush dot(Gdiplus::Color(255, 0x4F, 0x9B, 0xFF));
                    g.FillEllipse(&dot, mx - 3 * s, my - 3 * s, 6 * s, 6 * s);
                }
            }
            else if (item.kind == MenuItemKind::Check && item.checked) {
                Gdiplus::Pen tick(Gdiplus::Color(255, 0x4A, 0xDE, 0x80), 2.0f * s);
                tick.SetStartCap(Gdiplus::LineCapRound);
                tick.SetEndCap(Gdiplus::LineCapRound);
                tick.SetLineJoin(Gdiplus::LineJoinRound);
                Gdiplus::PointF pts[] = {
                    { mx - 5 * s, my },
                    { mx - 1.5f * s, my + 3.5f * s },
                    { mx + 5 * s, my - 4 * s },
                };
                g.DrawLines(&tick, pts, 3);
            }

            Gdiplus::REAL shortcutW = 0;
            if (!item.shortcut.isEmpty()) {
                const std::wstring sc = wide(item.shortcut);
                Gdiplus::RectF measured;
                g.MeasureString(sc.c_str(), -1, &shortcutFont, Gdiplus::PointF(0, 0), &measured);
                shortcutW = measured.Width;
                Gdiplus::RectF sr(rf.X, rf.Y, rf.Width - textPad, rf.Height);
                g.DrawString(sc.c_str(), -1, &shortcutFont, sr, &rightFmt, &shortcutBrush);
            }

            const std::wstring label = wide(item.label);
            Gdiplus::RectF lr(rf.X + markerW, rf.Y,
                              rf.Width - markerW - textPad - shortcutW - (shortcutW > 0 ? 12 * s : 0),
                              rf.Height);
            g.DrawString(label.c_str(), -1, &labelFont, lr, &leftFmt, &labelBrush);
            break;
        }
        }
    }

    POINT pos = { m_TopLeft.x(), m_TopLeft.y() };
    canvas.present(m_Hwnd, pos, 255);
}

LRESULT CALLBACK StreamMenuWindow::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto self = (StreamMenuWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self == nullptr) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return self->handleMessage(msg, wParam, lParam);
}

LRESULT StreamMenuWindow::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_MOUSEMOVE:
        if (m_Open) {
            const QPoint local = localPointFromLParam(lParam);
            setHighlight(containsLocal(local) ? hitTestMenu(m_Layout, m_Items, local) : -1);
            // With capture held the system does not update the cursor shape
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (m_Open) {
            const QPoint local = localPointFromLParam(lParam);
            if (!containsLocal(local)) {
                dismiss();
            }
            else {
                m_PressedIndex = hitTestMenu(m_Layout, m_Items, local);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (m_Open) {
            const QPoint local = localPointFromLParam(lParam);
            const int index = containsLocal(local) ? hitTestMenu(m_Layout, m_Items, local) : -1;
            const int pressed = m_PressedIndex;
            m_PressedIndex = -1;
            if (index >= 0 && index == pressed) {
                activate(index);
            }
        }
        return 0;

    case WM_CAPTURECHANGED:
        // Someone else took the mouse (Alt+Tab, another window): close quietly
        if (m_Open && (HWND)lParam != m_Hwnd) {
            dismiss();
        }
        return 0;
    }

    return DefWindowProcW(m_Hwnd, msg, wParam, lParam);
}

}
