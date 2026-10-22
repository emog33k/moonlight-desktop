#pragma once

#include "menulayout.h"
#include "streammenumodel.h"

#include <QRect>
#include <QVector>

#include <functional>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace LocalRdp {

// The dark menu panel that opens next to the floating button. It holds mouse
// capture while open, so a click anywhere else just closes it (and never
// reaches the stream). It never takes keyboard focus; the stream window
// forwards Esc/arrows/Enter through the controller.
class StreamMenuWindow
{
public:
    // onAction: an item was chosen (the menu has already closed).
    // onDismissed: the menu closed itself without a choice (click outside, capture lost).
    StreamMenuWindow(HWND owner,
                     std::function<void(StreamAction, int)> onAction,
                     std::function<void()> onDismissed);
    ~StreamMenuWindow();

    StreamMenuWindow(const StreamMenuWindow&) = delete;
    StreamMenuWindow& operator=(const StreamMenuWindow&) = delete;

    // Opens (or, if already open, replaces) the panel next to the button.
    void show(const QRect& buttonScreenRect, Edge buttonEdge, const QVector<MenuItem>& items, int gap = 6);
    // Closes without calling any callback.
    void close();
    bool isOpen() const { return m_Open; }

    // Pointer moves and presses outside the panel while it is open (screen
    // coordinates), e.g. over the toolbar. Returning true from a press closes the
    // menu without onDismissed (the handler takes over). Runs inside the window
    // procedure: post only.
    void setOutsideHandler(std::function<bool(QPoint, bool)> handler) { m_OnOutside = std::move(handler); }

    // Keyboard navigation
    void moveHighlight(int dir);
    void activateHighlighted();
    // Left/Right on a highlighted slider move it by delta (true if there is one)
    bool nudgeHighlightedSlider(int delta);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void render();
    void setHighlight(int index);
    void activate(int index);
    void dismiss();
    void setSliderValue(int index, int value);
    QPoint localPointFromLParam(LPARAM lParam) const;
    bool containsLocal(QPoint local) const;

    HWND m_Owner;
    HWND m_Hwnd = nullptr;
    std::function<void(StreamAction, int)> m_OnAction;
    std::function<void()> m_OnDismissed;
    std::function<bool(QPoint, bool)> m_OnOutside;

    QVector<MenuItem> m_Items;
    MenuLayout m_Layout;
    double m_Scale = 1.0;
    QPoint m_TopLeft;
    bool m_Open = false;
    int m_Highlight = -1;
    int m_PressedIndex = -1;
    int m_SliderDrag = -1; // slider being dragged
};

}
