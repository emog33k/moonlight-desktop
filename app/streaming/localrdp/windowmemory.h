#pragma once

#include <QRect>
#include <QString>
#include <QVector>

#include <optional>

// Where the stream window was (and how big) for each PC, so the next stream
// opens there again. Rectangles are the window's client area, as SDL uses them.

namespace LocalRdp {

struct WindowPlacement {
    QRect rect;
    bool maximized = false;
};

std::optional<WindowPlacement> loadWindowPlacement(const QString& hostId);
void saveWindowPlacement(const QString& hostId, const WindowPlacement& placement);

// Usable: a sensible size, and the top of the window (where it is grabbed)
// on one of the monitors there are now.
bool placementFits(const QRect& rect, const QVector<QRect>& usableScreens);

}
