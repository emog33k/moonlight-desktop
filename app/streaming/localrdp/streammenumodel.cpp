#include "streammenumodel.h"

namespace LocalRdp {

static MenuItem header(const QString& label)
{
    return { MenuItemKind::Header, label, QString(), std::nullopt, false };
}

static MenuItem separator()
{
    return { MenuItemKind::Separator, QString(), QString(), std::nullopt, false };
}

static MenuItem item(MenuItemKind kind, const QString& label, const QString& shortcut,
                     StreamAction action, bool checked = false)
{
    return { kind, label, shortcut, action, checked };
}

QVector<MenuItem> buildStreamMenu(const StreamState& s)
{
    using K = MenuItemKind;
    using A = StreamAction;

    return {
        header(QStringLiteral("Мышь")),
        item(K::Radio, QStringLiteral("Режим рабочего стола"), QString(), A::SetDesktopMouseMode, s.absoluteMouse),
        item(K::Radio, QStringLiteral("Игровой режим"), QStringLiteral("Ctrl+Alt+Shift+M"), A::SetGameMouseMode, !s.absoluteMouse),
        item(K::Check, QStringLiteral("Показывать мой курсор"), QStringLiteral("Ctrl+Alt+Shift+C"), A::ToggleLocalCursor, s.localCursorVisible),
        item(K::Check, QStringLiteral("Не выпускать курсор из окна"), QStringLiteral("Ctrl+Alt+Shift+L"), A::TogglePointerLock, s.pointerLocked),
        item(K::Command, QStringLiteral("Курсор ПК: показать/скрыть"), QStringLiteral("Ctrl+Alt+Shift+N"), A::ToggleHostCursor),
        separator(),
        header(QStringLiteral("Клавиатура")),
        item(K::Check, QStringLiteral("Передавать Alt+Tab, Win на ПК"), QStringLiteral("Ctrl+Alt+Shift+K"), A::ToggleSystemKeyCapture, s.systemKeyCapture),
        item(K::Command, QStringLiteral("Напечатать текст из буфера ноутбука"), QStringLiteral("Ctrl+Alt+Shift+V"), A::PasteClipboardText),
        separator(),
        header(QStringLiteral("Экран")),
        item(K::Check, QStringLiteral("Полный экран"), QStringLiteral("Ctrl+Alt+Shift+X"), A::ToggleFullscreen, s.fullscreen),
        item(K::Check, QStringLiteral("Статистика"), QStringLiteral("Ctrl+Alt+Shift+S"), A::ToggleStats, s.statsVisible),
        separator(),
        item(K::Command, QStringLiteral("Свернуть"), QStringLiteral("Ctrl+Alt+Shift+D"), A::Minimize),
        item(K::Command, QStringLiteral("Отключиться"), QStringLiteral("Ctrl+Alt+Shift+Q"), A::Disconnect),
        item(K::Command, QStringLiteral("Отключиться и закрыть Moonlight"), QStringLiteral("Ctrl+Alt+Shift+E"), A::DisconnectAndExit),
    };
}

}
