#pragma once

#include "streammenumodel.h"

#include <QRect>
#include <QString>
#include <QVector>

#include <functional>

// The AnyDesk-style toolbar at the top of the stream window: what is on it and
// where. Pure logic, unit tested; the Win32 window only draws it.

namespace LocalRdp {

struct ToolbarButton {
    enum class Kind { Section, Action };
    Kind kind;
    MenuSection section = MenuSection::Mouse; // Kind::Section: opens this dropdown
    StreamAction action = StreamAction::Count; // Kind::Action: runs this
    wchar_t icon = 0;                          // Segoe MDL2 Assets glyph
    QString label;                             // empty: icon only
    bool rightSide = false;
    int param = 0;                             // Kind::Action: e.g. the monitor (Sunshine's index)
    bool selected = false;                     // drawn as the current one (the monitor shown)

    bool operator==(const ToolbarButton& o) const
    {
        return kind == o.kind && section == o.section && action == o.action && icon == o.icon && label == o.label &&
               rightSide == o.rightSide && param == o.param && selected == o.selected;
    }
};

// Left: the section dropdowns, then (with two or more monitors) one button per
// PC monitor, numbered like Windows, the one shown marked. Right: full screen,
// minimize, disconnect.
QVector<ToolbarButton> toolbarButtons(const QVector<DisplayInfo>& displays = {}, int currentDisplay = -1);

// Toolbar height at 96 DPI.
constexpr int kToolbarBaseHeight = 34;

struct ToolbarLayout {
    QSize size;
    QVector<QRect> rects; // one per button, relative to the toolbar; empty rect = no room
};

// textWidth(label) measures a label in pixels (the window passes GDI+ measuring).
// compact (full screen): only as wide as the buttons need, at most `width`.
ToolbarLayout layoutToolbar(const QVector<ToolbarButton>& buttons, int width, double scale,
                            const std::function<int(const QString&)>& textWidth, bool compact = false);

// Index of the button under a toolbar-relative point, or -1.
int hitTestToolbar(const ToolbarLayout& layout, QPoint local);

}
