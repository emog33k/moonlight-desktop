#pragma once

#include "buttonplacement.h"
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
                     std::function<void(StreamAction)> onAction,
                     std::function<void()> onDismissed);
    ~StreamMenuWindow();

    StreamMenuWindow(const StreamMenuWindow&) = delete;
    StreamMenuWindow& operator=(const StreamMenuWindow&) = delete;

    void show(const QRect& buttonScreenRect, Edge buttonEdge, const QVector<MenuItem>& items);
    // Closes without calling any callback.
    void close();
    bool isOpen() const { return m_Open; }

    // Keyboard navigation
    void moveHighlight(int dir);
    void activateHighlighted();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void render();
    void setHighlight(int index);
    void activate(int index);
    void dismiss();
    QPoint localPointFromLParam(LPARAM lParam) const;
    bool containsLocal(QPoint local) const;

    HWND m_Owner;
    HWND m_Hwnd = nullptr;
    std::function<void(StreamAction)> m_OnAction;
    std::function<void()> m_OnDismissed;

    QVector<MenuItem> m_Items;
    MenuLayout m_Layout;
    double m_Scale = 1.0;
    QPoint m_TopLeft;
    bool m_Open = false;
    int m_Highlight = -1;
    int m_PressedIndex = -1;
};

}
