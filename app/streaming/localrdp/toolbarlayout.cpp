#include "toolbarlayout.h"

#include <algorithm>
#include <cmath>

namespace LocalRdp {

QVector<ToolbarButton> toolbarButtons(const QVector<DisplayInfo>& displays, int currentDisplay)
{
    using K = ToolbarButton::Kind;
    auto section = [](MenuSection s, wchar_t icon, const QString& label) {
        ToolbarButton b{ K::Section };
        b.section = s;
        b.icon = icon;
        b.label = label;
        return b;
    };
    auto action = [](StreamAction a, wchar_t icon) {
        ToolbarButton b{ K::Action };
        b.action = a;
        b.icon = icon;
        b.rightSide = true;
        return b;
    };
    QVector<ToolbarButton> buttons = {
        section(MenuSection::Mouse, 0xE962, QStringLiteral("Мышь")),
        section(MenuSection::Keyboard, 0xE765, QStringLiteral("Клавиатура")),
        section(MenuSection::Clipboard, 0xE77F, QStringLiteral("Буфер")),
        section(MenuSection::Screen, 0xE7F4, QStringLiteral("Экран")),
        section(MenuSection::Sound, 0xE767, QStringLiteral("Звук")),
        section(MenuSection::Actions, 0xE945, QStringLiteral("Действия")),
        section(MenuSection::Pc, 0xE977, QStringLiteral("ПК")),
    };

    // Monitors after the sections, so the sections' positions never change
    if (displays.size() >= 2) {
        // In the order they stand on the PC's desk (as in Settings > Display)
        for (int i : displaysInWindowsOrder(displays)) {
            ToolbarButton b{ K::Action };
            b.action = StreamAction::SwitchDisplay;
            b.param = i;
            b.icon = 0xE7F4;
            b.label = QString::number(windowsDisplayNumber(displays[i].name, i));
            b.selected = i == currentDisplay;
            buttons << b;
        }
    }

    buttons << action(StreamAction::ToggleFullscreen, 0xE740)
            << action(StreamAction::Minimize, 0xE921)
            << action(StreamAction::Disconnect, 0xE711);
    return buttons;
}

ToolbarLayout layoutToolbar(const QVector<ToolbarButton>& buttons, int width, double scale,
                            const std::function<int(const QString&)>& textWidth, bool compact)
{
    auto px = [scale](int v) { return (int)std::lround(v * scale); };

    if (compact) {
        // Natural width: the left group, a gap, the right group
        int natural = px(4) + px(24);
        for (const ToolbarButton& b : buttons) {
            natural += b.rightSide ? px(46)
                                   : px(10) * 2 + px(16) + (b.label.isEmpty() ? 0 : px(6) + textWidth(b.label)) + px(2);
        }
        width = qMin(width, natural);
    }

    ToolbarLayout l;
    const int height = px(kToolbarBaseHeight);
    l.size = QSize(qMax(0, width), height);
    l.rects.resize(buttons.size());

    // Right group, laid out from the right edge so disconnect is always there
    const int iconButtonWidth = px(46);
    int x = width;
    for (int i = buttons.size() - 1; i >= 0; i--) {
        if (!buttons[i].rightSide) {
            continue;
        }
        x -= iconButtonWidth;
        l.rects[i] = QRect(x, 0, iconButtonWidth, height);
    }
    const int rightStart = x;

    // Left group: icon + label; a button that would reach the right group is dropped
    const int pad = px(10), iconWidth = px(16), iconGap = px(6), spacing = px(2);
    int left = px(4);
    for (int i = 0; i < buttons.size(); i++) {
        if (buttons[i].rightSide) {
            continue;
        }
        int w = pad * 2 + iconWidth;
        if (!buttons[i].label.isEmpty()) {
            w += iconGap + textWidth(buttons[i].label);
        }
        if (left + w > rightStart - spacing) {
            break;
        }
        l.rects[i] = QRect(left, 0, w, height);
        left += w + spacing;
    }
    return l;
}

int hitTestToolbar(const ToolbarLayout& layout, QPoint local)
{
    for (int i = 0; i < layout.rects.size(); i++) {
        if (!layout.rects[i].isEmpty() && layout.rects[i].contains(local)) {
            return i;
        }
    }
    return -1;
}

}
