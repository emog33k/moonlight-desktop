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
    void setVisible(bool visible);
    bool isVisible() const { return m_Visible; }

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

    HWND m_Owner;
    HWND m_Hwnd = nullptr;
    HWINEVENTHOOK m_LocationHook = nullptr;
    std::function<void()> m_OnClick;

    ButtonPlacement m_Placement;
    QPoint m_TopLeft;
    int m_Size = 0;
    bool m_Visible = false;
    bool m_Hover = false;
    bool m_Pressed = false;
    bool m_Dragging = false;
    QPoint m_PressScreenPos;
    QPoint m_PressOffset;
};

}
