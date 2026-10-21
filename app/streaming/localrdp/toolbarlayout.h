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
};

// Left: the section dropdowns. Right: full screen, minimize, disconnect.
QVector<ToolbarButton> toolbarButtons();

// Toolbar height at 96 DPI.
constexpr int kToolbarBaseHeight = 34;

struct ToolbarLayout {
    QSize size;
    QVector<QRect> rects; // one per button, relative to the toolbar; empty rect = no room
};

// textWidth(label) measures a label in pixels (the window passes GDI+ measuring).
ToolbarLayout layoutToolbar(const QVector<ToolbarButton>& buttons, int width, double scale,
                            const std::function<int(const QString&)>& textWidth);

// Index of the button under a toolbar-relative point, or -1.
int hitTestToolbar(const ToolbarLayout& layout, QPoint local);

}
