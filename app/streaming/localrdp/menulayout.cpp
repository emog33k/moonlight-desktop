#include "menulayout.h"

#include <algorithm>
#include <cmath>

namespace LocalRdp {

static int scaled(int px, double scale)
{
    return (int)std::lround(px * scale);
}

static bool isActionable(const MenuItem& item)
{
    return item.kind == MenuItemKind::Radio ||
           item.kind == MenuItemKind::Check ||
           item.kind == MenuItemKind::Command;
}

MenuLayout layoutMenu(const QVector<MenuItem>& items, double scale)
{
    const int pad = scaled(6, scale);
    const int width = scaled(380, scale);

    MenuLayout layout;
    int y = pad;
    for (const MenuItem& item : items) {
        int h;
        switch (item.kind) {
        case MenuItemKind::Header:
            h = scaled(26, scale);
            break;
        case MenuItemKind::Separator:
            h = scaled(9, scale);
            break;
        default:
            h = scaled(30, scale);
            break;
        }
        layout.itemRects << QRect(pad, y, width - 2 * pad, h);
        y += h;
    }
    layout.size = QSize(width, y + pad);
    return layout;
}

int hitTestMenu(const MenuLayout& layout, const QVector<MenuItem>& items, QPoint local)
{
    for (int i = 0; i < layout.itemRects.size() && i < items.size(); i++) {
        if (layout.itemRects[i].contains(local)) {
            return isActionable(items[i]) ? i : -1;
        }
    }
    return -1;
}

int nextActionable(const QVector<MenuItem>& items, int from, int dir)
{
    const int n = items.size();
    if (n == 0) {
        return -1;
    }
    int i = from;
    if (i < 0) {
        i = dir > 0 ? -1 : n;
    }
    for (int step = 0; step < n; step++) {
        i = ((i + dir) % n + n) % n;
        if (isActionable(items[i])) {
            return i;
        }
    }
    return -1;
}

static int clampInto(int pos, int start, int length, int size)
{
    pos = std::min(pos, start + length - size);
    return std::max(pos, start);
}

QPoint placeMenu(const QRect& button, Edge buttonEdge, QSize menuSize, const QRect& workArea, int gap)
{
    const int bx = button.x(), by = button.y(), bw = button.width(), bh = button.height();
    int x = bx, y = by;

    switch (buttonEdge) {
    case Edge::Right:
        x = bx - gap - menuSize.width();
        break;
    case Edge::Left:
        x = bx + bw + gap;
        break;
    case Edge::Top:
        y = by + bh + gap;
        break;
    case Edge::Bottom:
        y = by - gap - menuSize.height();
        break;
    }

    return QPoint(clampInto(x, workArea.x(), workArea.width(), menuSize.width()),
                  clampInto(y, workArea.y(), workArea.height(), menuSize.height()));
}

}
