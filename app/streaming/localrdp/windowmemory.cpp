#include "windowmemory.h"

#include <QSettings>

namespace LocalRdp {

static QString key(const QString& hostId)
{
    return QStringLiteral("localrdp/window/") + (hostId.isEmpty() ? QStringLiteral("default") : hostId);
}

std::optional<WindowPlacement> loadWindowPlacement(const QString& hostId)
{
    QSettings settings;
    const QRect rect = settings.value(key(hostId) + "/rect").toRect();
    if (!rect.isValid()) {
        return std::nullopt;
    }
    return WindowPlacement{ rect, settings.value(key(hostId) + "/maximized", false).toBool() };
}

void saveWindowPlacement(const QString& hostId, const WindowPlacement& placement)
{
    QSettings settings;
    settings.setValue(key(hostId) + "/rect", placement.rect);
    settings.setValue(key(hostId) + "/maximized", placement.maximized);
}

bool placementFits(const QRect& rect, const QVector<QRect>& usableScreens)
{
    if (!rect.isValid() || rect.width() < 320 || rect.height() < 200) {
        return false;
    }
    // The strip at the top (title bar and toolbar) must be reachable
    const QRect top(rect.left(), rect.top(), rect.width(), 40);
    for (const QRect& screen : usableScreens) {
        const QRect visible = screen.intersected(top);
        if (visible.width() >= 100 && visible.height() >= 20) {
            return true;
        }
    }
    return false;
}

}
