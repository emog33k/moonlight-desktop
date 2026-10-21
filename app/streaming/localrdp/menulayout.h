#pragma once

#include "buttonplacement.h"
#include "streammenumodel.h"

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>

// Geometry of the in-stream menu panel. Pure logic, unit tested; the Win32
// window only draws what this computes.

namespace LocalRdp {

struct MenuLayout {
    QSize size;               // whole panel
    QVector<QRect> itemRects; // one per item, relative to the panel's top-left
};

// Sizes at 96 DPI (multiplied by scale): padding 6, width 380,
// header row 26, item row 30, separator row 9, status row 24.
MenuLayout layoutMenu(const QVector<MenuItem>& items, double scale);

// Index of the enabled actionable item (radio/check/command) under a panel-relative point, or -1.
int hitTestMenu(const MenuLayout& layout, const QVector<MenuItem>& items, QPoint local);

// Next actionable item after `from` in direction dir (+1 down, -1 up), wrapping around.
// from = -1 starts before the first item. Returns -1 if there is none.
int nextActionable(const QVector<MenuItem>& items, int from, int dir);

// Top-left of the panel: beside the button on the side facing into the window,
// aligned with the button, then kept fully inside the monitor's work area.
QPoint placeMenu(const QRect& button, Edge buttonEdge, QSize menuSize, const QRect& workArea, int gap);

}
