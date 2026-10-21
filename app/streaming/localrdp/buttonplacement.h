#pragma once

#include <QPoint>
#include <QRect>
#include <QSettings>

// Geometry of the floating menu button: which edge of the stream window it sticks
// to and how far along that edge. Pure logic (Qt Core only), unit tested.

namespace LocalRdp {

enum class Edge { Left = 0, Right = 1, Top = 2, Bottom = 3 };

struct ButtonPlacement {
    Edge edge = Edge::Right;
    double fraction = 0.4; // 0 = start of the edge (top/left), 1 = end
};

// Top-left corner of a size x size button inside client (screen coordinates),
// margin pixels away from its edge. A button larger than the window starts at
// the window's top-left corner.
QPoint placeButton(const QRect& client, int size, int margin, ButtonPlacement p);

// Nearest edge to the button's center and the position along it, fraction in [0, 1].
ButtonPlacement snapToNearestEdge(const QRect& client, QPoint center, int size);

// True if the pointer moved at most threshold pixels on both axes (a click, not a drag).
bool isClick(QPoint down, QPoint up, int threshold = 4);

// Keys "localrdp/button/edge" and "localrdp/button/fraction"; invalid values give the default.
ButtonPlacement loadPlacement(QSettings& settings);
void savePlacement(QSettings& settings, ButtonPlacement p);

}
