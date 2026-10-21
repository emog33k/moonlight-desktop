#pragma once

#include "buttonplacement.h"

#include <QRect>

#include <functional>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace LocalRdp {

// Small round "menu" button floating over the stream window (owned popup,
// never takes keyboard focus). Click calls onClick; drag moves it, and on
// release it snaps to the nearest edge and remembers the spot.
class FloatingButtonWindow
{
public:
    FloatingButtonWindow(HWND owner, std::function<void()> onClick);
    ~FloatingButtonWindow();

    FloatingButtonWindow(const FloatingButtonWindow&) = delete;
    FloatingButtonWindow& operator=(const FloatingButtonWindow&) = delete;

    // Moves the button to its saved spot inside the owner's current client area.
    void reposition();
    // Follows the stream window (hidden while it is minimized).
    void setVisible(bool visible);
    bool isVisible() const { return m_Visible; }

    // Auto-hide (used in full screen): the button only shows while the pointer is
    // near its spot, and hides again a second after the pointer leaves.
    void setAutoHide(bool autoHide);
    // While the menu is open the button never auto-hides.
    void setKeepVisible(bool keep);
    // Where the pointer is (GetCursorPos by default; tests substitute it).
    void setCursorProvider(std::function<QPoint()> provider);

    // Where the button is on screen (used to place the menu next to it).
    QRect screenRect() const;
    // The window edge the button sits on.
    Edge edge() const { return m_Placement.edge; }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void CALLBACK winEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                      LONG idObject, LONG idChild, DWORD thread, DWORD time);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    QRect ownerClientScreenRect() const;
    QPoint screenPointFromLParam(LPARAM lParam) const;
    int buttonSize() const;
    int margin() const;
    void render();
    void moveTo(QPoint topLeft);
    void applyVisibility();
    void onAutoHideTimer();

    HWND m_Owner;
    HWND m_Hwnd = nullptr;
    HWINEVENTHOOK m_LocationHook = nullptr;
    std::function<void()> m_OnClick;

    ButtonPlacement m_Placement;
    QPoint m_TopLeft;
    int m_Size = 0;
    bool m_Visible = false;          // the stream window is shown (not minimized)
    bool m_AutoHide = false;
    bool m_KeepVisible = false;
    bool m_NearPointer = true;       // auto-hide: pointer recently near the button
    unsigned long long m_LastNearTick = 0;
    bool m_Shown = false;            // actual window state
    std::function<QPoint()> m_CursorProvider;
    bool m_Hover = false;
    bool m_Pressed = false;
    bool m_Dragging = false;
    QPoint m_PressScreenPos;
    QPoint m_PressOffset;
};

}
