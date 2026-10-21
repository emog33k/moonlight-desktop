#pragma once

#include "streamactions.h"
#include "toolbarlayout.h"

#include <QPoint>
#include <QRect>
#include <QVector>

#include <functional>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace LocalRdp {

// The AnyDesk-style toolbar: a dark strip across the top of the stream window's
// client area (an owned layered popup that never takes keyboard focus). The
// video is drawn below it (see videoarea.h), so the mouse over the toolbar never
// moves the PC's cursor.
//
// In full screen it hides and shows while the pointer is at the top edge of the
// screen (or a menu from it is open).
class ToolbarWindow
{
public:
    // onPress(index): a button was pressed (sections open on press, actions on
    // release over the same button). Runs inside the window procedure: post only.
    ToolbarWindow(HWND owner, std::function<void(int)> onPress);
    ~ToolbarWindow();

    ToolbarWindow(const ToolbarWindow&) = delete;
    ToolbarWindow& operator=(const ToolbarWindow&) = delete;

    const QVector<ToolbarButton>& buttons() const { return m_Buttons; }

    // Follows the stream window (hidden while it is minimized).
    void setVisible(bool visible);
    // Full screen: auto-hide at the top edge instead of a reserved strip.
    void setFullscreen(bool fullscreen);
    bool isFullscreen() const { return m_Fullscreen; }
    // While a menu from the toolbar is open it never auto-hides.
    void setKeepVisible(bool keep);
    // The button whose menu is open is drawn pressed (-1: none).
    void setActiveButton(int index);
    void setAgentStatus(AgentStatus status);
    // Polled a few times a second to keep the status dot current.
    void setStatusProvider(std::function<AgentStatus()> provider) { m_StatusProvider = std::move(provider); }
    // Where the pointer is (GetCursorPos by default; tests substitute it).
    void setCursorProvider(std::function<QPoint()> provider);

    // Sizes/positions the strip over the owner's client area.
    void reposition();

    // Strip height in pixels at the owner's DPI.
    int height() const;
    bool isShown() const { return m_Shown; }
    // Screen rect of a button (empty when it does not fit).
    QRect buttonScreenRect(int index) const;
    // Button under a screen point while shown, or -1.
    int buttonAtScreen(QPoint screen) const;

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void CALLBACK winEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                      LONG idObject, LONG idChild, DWORD thread, DWORD time);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    QRect ownerClientScreenRect() const;
    QPoint cursorPos() const;
    void relayout(int width);
    void render();
    void applyVisibility();
    void onTimer();
    void setHover(int index);

    HWND m_Owner;
    HWND m_Hwnd = nullptr;
    HWINEVENTHOOK m_LocationHook = nullptr;
    std::function<void(int)> m_OnPress;
    QVector<ToolbarButton> m_Buttons;
    ToolbarLayout m_Layout;
    double m_Scale = 1.0;
    QPoint m_TopLeft;

    bool m_Visible = false;      // the stream window is shown (not minimized)
    bool m_Fullscreen = false;
    bool m_KeepVisible = false;
    bool m_PointerNear = false;  // full screen: pointer at the top edge or on the strip lately
    unsigned long long m_LastNearTick = 0;
    bool m_Shown = false;
    std::function<QPoint()> m_CursorProvider;
    std::function<AgentStatus()> m_StatusProvider;

    AgentStatus m_AgentStatus = AgentStatus::Unknown;
    int m_Hover = -1;
    int m_Pressed = -1;          // action button held down
    int m_Active = -1;           // section whose menu is open
};

}
