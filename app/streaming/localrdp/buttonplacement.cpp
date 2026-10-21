#include "buttonplacement.h"

#include <algorithm>
#include <cmath>

namespace LocalRdp {

static const char* const kEdgeKey = "localrdp/button/edge";
static const char* const kFractionKey = "localrdp/button/fraction";

static double clamp01(double v)
{
    if (!(v >= 0.0)) { // also catches NaN
        return 0.0;
    }
    return std::min(v, 1.0);
}

// Keeps a coordinate so the button stays inside [start, start + length).
// A button larger than the window is pinned to the window's start.
static int clampInside(int pos, int start, int length, int size)
{
    pos = std::min(pos, start + length - size);
    return std::max(pos, start);
}

QPoint placeButton(const QRect& client, int size, int margin, ButtonPlacement p)
{
    const double f = clamp01(p.fraction);
    const int alongX = client.left() + (int)std::lround(f * (client.width() - size));
    const int alongY = client.top() + (int)std::lround(f * (client.height() - size));

    int x = 0, y = 0;
    switch (p.edge) {
    case Edge::Left:
        x = client.left() + margin;
        y = alongY;
        break;
    case Edge::Right:
        x = client.left() + client.width() - size - margin;
        y = alongY;
        break;
    case Edge::Top:
        x = alongX;
        y = client.top() + margin;
        break;
    case Edge::Bottom:
        x = alongX;
        y = client.top() + client.height() - size - margin;
        break;
    }

    return QPoint(clampInside(x, client.left(), client.width(), size),
                  clampInside(y, client.top(), client.height(), size));
}

static double fractionAlong(int center, int start, int length, int size)
{
    const int travel = length - size;
    if (travel <= 0) {
        return 0.0;
    }
    return clamp01((center - start - size / 2.0) / travel);
}

ButtonPlacement snapToNearestEdge(const QRect& client, QPoint center, int size)
{
    const int dLeft = center.x() - client.left();
    const int dRight = client.left() + client.width() - center.x();
    const int dTop = center.y() - client.top();
    const int dBottom = client.top() + client.height() - center.y();

    ButtonPlacement p;
    const int best = std::min({ dLeft, dRight, dTop, dBottom });
    if (best == dRight) {
        p.edge = Edge::Right;
    }
    else if (best == dLeft) {
        p.edge = Edge::Left;
    }
    else if (best == dTop) {
        p.edge = Edge::Top;
    }
    else {
        p.edge = Edge::Bottom;
    }

    if (p.edge == Edge::Left || p.edge == Edge::Right) {
        p.fraction = fractionAlong(center.y(), client.top(), client.height(), size);
    }
    else {
        p.fraction = fractionAlong(center.x(), client.left(), client.width(), size);
    }
    return p;
}

bool isClick(QPoint down, QPoint up, int threshold)
{
    return std::abs(up.x() - down.x()) <= threshold && std::abs(up.y() - down.y()) <= threshold;
}

ButtonPlacement loadPlacement(QSettings& settings)
{
    ButtonPlacement p;

    bool ok = false;
    const int edge = settings.value(kEdgeKey).toInt(&ok);
    if (ok && edge >= (int)Edge::Left && edge <= (int)Edge::Bottom) {
        p.edge = (Edge)edge;
    }

    const double fraction = settings.value(kFractionKey).toDouble(&ok);
    if (ok) {
        p.fraction = clamp01(fraction);
    }
    return p;
}

void savePlacement(QSettings& settings, ButtonPlacement p)
{
    settings.setValue(kEdgeKey, (int)p.edge);
    settings.setValue(kFractionKey, clamp01(p.fraction));
}

}
