#include "streammenumodel.h"

#include <QRegularExpression>

#include <algorithm>

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

static MenuItem status(const QString& text)
{
    return { MenuItemKind::Status, text, QString(), std::nullopt, false };
}

static MenuItem keys(const QString& label, QuickKeys k, const QString& shortcut)
{
    MenuItem m = { MenuItemKind::Command, label, shortcut, StreamAction::SendKeys, false };
    m.param = (int)k;
    return m;
}

// Restart/shutdown: the first click only asks again (the menu stays open)
static MenuItem power(const QString& label, const QString& confirmLabel, StreamAction action, bool confirming, bool enabled)
{
    MenuItem m = { MenuItemKind::Command, confirming ? confirmLabel : label, QString(), action, false };
    m.keepOpen = true;
    m.enabled = enabled;
    return m;
}

static MenuItem disabledUnless(MenuItem item, bool enabled)
{
    item.enabled = enabled;
    return item;
}

static QString agentStatusText(AgentStatus status)
{
    switch (status) {
    case AgentStatus::Connecting:
        return QStringLiteral("Агент на ПК: подключение…");
    case AgentStatus::Connected:
        return QStringLiteral("Агент на ПК: подключён");
    case AgentStatus::NotFound:
        return QStringLiteral("Агент на ПК: не найден");
    case AgentStatus::CertError:
        return QStringLiteral("Агент на ПК: ошибка сертификата");
    case AgentStatus::Rejected:
        return QStringLiteral("Агент на ПК: ПК отклонил подключение");
    case AgentStatus::Unknown:
        break;
    }
    return QStringLiteral("Агент на ПК: —");
}

QVector<QualityPreset> qualityPresets()
{
    return {
        { QStringLiteral("Экономно (слабая сеть)"), 8000, 30 },
        { QStringLiteral("Обычно"), 20000, 60 },
        { QStringLiteral("Максимум"), 60000, 60 },
    };
}

int windowsDisplayNumber(const QString& deviceName, int position)
{
    static const QRegularExpression number(QStringLiteral("DISPLAY(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = number.match(deviceName);
    const int n = m.hasMatch() ? m.captured(1).toInt() : 0;
    return n > 0 ? n : position + 1;
}

static QVector<MenuItem> displayItems(const StreamState& s)
{
    QVector<MenuItem> items;
    // Sunshine switches the captured monitor on Ctrl+Alt+Shift+F1..F12 (its own
    // order); the menu shows and orders them by Windows' numbers.
    if (s.displays.isEmpty()) {
        for (int i = 0; i < 4; i++) {
            MenuItem m = item(MenuItemKind::Radio, QStringLiteral("Монитор %1").arg(i + 1),
                              QStringLiteral("Ctrl+Alt+Shift+F%1").arg(i + 1), StreamAction::SwitchDisplay, false);
            m.param = i;
            items << m;
        }
        return items;
    }
    QVector<int> order;
    for (int i = 0; i < qMin(s.displays.size(), 12); i++) {
        order << i;
    }
    std::stable_sort(order.begin(), order.end(), [&s](int a, int b) {
        return windowsDisplayNumber(s.displays[a].name, a) < windowsDisplayNumber(s.displays[b].name, b);
    });
    for (int i : order) {
        const DisplayInfo& d = s.displays[i];
        QString label = QStringLiteral("Монитор %1: %2×%3").arg(windowsDisplayNumber(d.name, i)).arg(d.width).arg(d.height);
        if (d.primary) {
            label += QStringLiteral(", основной");
        }
        MenuItem m = item(MenuItemKind::Radio, label, QStringLiteral("Ctrl+Alt+Shift+F%1").arg(i + 1),
                          StreamAction::SwitchDisplay, i == s.currentDisplay);
        m.param = i;
        items << m;
    }
    return items;
}

QVector<MenuItem> buildSectionMenu(MenuSection section, const StreamState& s)
{
    using K = MenuItemKind;
    using A = StreamAction;

    switch (section) {
    case MenuSection::Mouse:
    {
        QVector<MenuItem> items = {
            header(QStringLiteral("Мышь")),
            item(K::Radio, QStringLiteral("Режим рабочего стола"), QString(), A::SetDesktopMouseMode, s.absoluteMouse),
            item(K::Radio, QStringLiteral("Игровой режим"), QStringLiteral("Ctrl+Alt+Shift+M"), A::SetGameMouseMode, !s.absoluteMouse),
            item(K::Check, QStringLiteral("Курсор как в AnyDesk"), QString(), A::ToggleLocalCursorShapes, s.localCursorShapes),
            item(K::Check, QStringLiteral("Не выпускать курсор из окна"), QStringLiteral("Ctrl+Alt+Shift+L"), A::TogglePointerLock, s.pointerLocked),
        };
        if (s.cursorSyncActive) {
            items << item(K::Command, QStringLiteral("Видно два курсора — убрать лишний"), QString(), A::FixDoubleCursor);
        }
        return items;
    }
    case MenuSection::Keyboard:
        return {
            header(QStringLiteral("Клавиатура")),
            item(K::Check, QStringLiteral("Передавать Alt+Tab, Win на ПК"), QStringLiteral("Ctrl+Alt+Shift+K"), A::ToggleSystemKeyCapture, s.systemKeyCapture),
            item(K::Command, QStringLiteral("Напечатать текст с ноутбука"), QStringLiteral("Ctrl+Alt+Shift+V"), A::PasteClipboardText),
        };
    case MenuSection::Clipboard:
        return {
            header(QStringLiteral("Буфер обмена")),
            item(K::Check, QStringLiteral("Синхронизация буфера обмена"), QString(), A::ToggleClipboardSync, s.clipboardSync),
            disabledUnless(item(K::Command, QStringLiteral("Отправить файлы на ПК…"), QString(), A::SendFilesToPc),
                           s.clipboardSync && s.agentStatus == AgentStatus::Connected),
        };
    case MenuSection::Screen:
    {
        QVector<MenuItem> items = { header(QStringLiteral("Монитор ПК")) };
        items += displayItems(s);
        items += {
            header(QStringLiteral("Экран")),
            item(K::Check, QStringLiteral("Полный экран"), QStringLiteral("Ctrl+Alt+Shift+X"), A::ToggleFullscreen, s.fullscreen),
            item(K::Check, QStringLiteral("Статистика"), QStringLiteral("Ctrl+Alt+Shift+S"), A::ToggleStats, s.statsVisible),
            header(QStringLiteral("Качество (стрим переподключится)")),
        };
        const QVector<QualityPreset> presets = qualityPresets();
        for (int i = 0; i < presets.size(); i++) {
            const QualityPreset& p = presets[i];
            MenuItem m = item(K::Radio, QStringLiteral("%1: %2 Мбит/с, %3 к/с").arg(p.name).arg(p.kbps / 1000).arg(p.fps),
                              QString(), A::SetQuality, s.bitrateKbps == p.kbps && s.fps == p.fps);
            m.param = i;
            items << m;
        }
        return items;
    }
    case MenuSection::Sound:
    {
        MenuItem onOff = item(K::Check, QStringLiteral("Звук включён"), QString(), A::ToggleStreamAudio, s.streamAudio);
        MenuItem up = disabledUnless(item(K::Command, QStringLiteral("Громче"), QStringLiteral("+10%"), A::VolumeUp),
                                     s.streamVolume < 100);
        MenuItem down = disabledUnless(item(K::Command, QStringLiteral("Тише"), QStringLiteral("−10%"), A::VolumeDown),
                                       s.streamVolume > 0);
        onOff.keepOpen = up.keepOpen = down.keepOpen = true;
        return {
            header(QStringLiteral("Звук с ПК")),
            onOff,
            status(QStringLiteral("Громкость: %1%").arg(s.streamVolume)),
            up,
            down,
        };
    }
    case MenuSection::Actions:
    {
        // Screenshots travel through the synced clipboard
        const bool viaClipboard = s.clipboardSync && s.agentStatus == AgentStatus::Connected;
        return {
            header(QStringLiteral("Снимок экрана")),
            disabledUnless(item(K::Command, QStringLiteral("Область ноутбука → в буфер ПК"), QString(), A::SnipLaptop),
                           viaClipboard),
            disabledUnless(keys(QStringLiteral("Область ПК → в буфер ноутбука"), QuickKeys::SnipArea, QStringLiteral("Win+Shift+S")),
                           viaClipboard),
            disabledUnless(keys(QStringLiteral("Весь экран ПК → в буфер ноутбука"), QuickKeys::PrintScreen, QStringLiteral("PrtSc")),
                           viaClipboard),
            header(QStringLiteral("Буфер обмена")),
            keys(QStringLiteral("Вставить на ПК"), QuickKeys::Paste, QStringLiteral("Ctrl+V")),
            header(QStringLiteral("Клавиши на ПК")),
            keys(QStringLiteral("Диспетчер задач"), QuickKeys::TaskManager, QStringLiteral("Ctrl+Shift+Esc")),
            keys(QStringLiteral("Свернуть все окна"), QuickKeys::ShowDesktop, QStringLiteral("Win+D")),
            keys(QStringLiteral("Проводник"), QuickKeys::Explorer, QStringLiteral("Win+E")),
            keys(QStringLiteral("Меню «Пуск»"), QuickKeys::Start, QStringLiteral("Win")),
            keys(QStringLiteral("Переключить окно"), QuickKeys::AltTab, QStringLiteral("Alt+Tab")),
        };
    }
    case MenuSection::Pc:
        return {
            header(QStringLiteral("ПК")),
            status(agentStatusText(s.agentStatus) +
                   (s.agentStatus == AgentStatus::Connected && s.pcLocked ? QStringLiteral(", ПК заблокирован") : QString())),
            disabledUnless(item(K::Command, QStringLiteral("Заблокировать ПК"), QString(), A::LockPc),
                           s.agentStatus == AgentStatus::Connected),
            item(K::Check, QStringLiteral("Блокировать ПК при отключении"), QString(), A::ToggleLockOnDisconnect, s.lockOnDisconnect),
            power(QStringLiteral("Перезагрузить ПК…"), QStringLiteral("Нажмите ещё раз: перезагрузить ПК"),
                  A::RebootPc, s.powerConfirm == PowerConfirm::Reboot, s.agentStatus == AgentStatus::Connected),
            power(QStringLiteral("Выключить ПК…"), QStringLiteral("Нажмите ещё раз: выключить ПК"),
                  A::ShutdownPc, s.powerConfirm == PowerConfirm::Shutdown, s.agentStatus == AgentStatus::Connected),
        };
    case MenuSection::Session:
        return {
            item(K::Command, QStringLiteral("Свернуть"), QStringLiteral("Ctrl+Alt+Shift+D"), A::Minimize),
            item(K::Command, QStringLiteral("Отключиться"), QStringLiteral("Ctrl+Alt+Shift+Q"), A::Disconnect),
            item(K::Command, QStringLiteral("Отключиться и закрыть Moonlight"), QStringLiteral("Ctrl+Alt+Shift+E"), A::DisconnectAndExit),
        };
    }
    return {};
}

QVector<short> quickKeyCombo(QuickKeys k)
{
    const short ctrl = 0xA2, shift = 0xA0, alt = 0xA4, win = 0x5B;
    switch (k) {
    case QuickKeys::Paste: return { ctrl, 0x56 };
    case QuickKeys::SnipArea: return { win, shift, 0x53 };
    case QuickKeys::PrintScreen: return { 0x2C };
    case QuickKeys::TaskManager: return { ctrl, shift, 0x1B };
    case QuickKeys::ShowDesktop: return { win, 0x44 };
    case QuickKeys::Explorer: return { win, 0x45 };
    case QuickKeys::Start: return { win };
    case QuickKeys::AltTab: return { alt, 0x09 };
    case QuickKeys::Count: break;
    }
    return {};
}

QVector<MenuItem> buildToolbarMenu(MenuSection section, const StreamState& s)
{
    QVector<MenuItem> items = buildSectionMenu(section, s);
    if (section == MenuSection::Pc) {
        items << separator();
        items += buildSectionMenu(MenuSection::Session, s);
    }
    return items;
}

QVector<MenuItem> buildStreamMenu(const StreamState& s)
{
    QVector<MenuItem> all;
    for (MenuSection section : { MenuSection::Mouse, MenuSection::Keyboard, MenuSection::Clipboard,
                                 MenuSection::Screen, MenuSection::Sound, MenuSection::Actions, MenuSection::Pc,
                                 MenuSection::Session }) {
        if (!all.isEmpty()) {
            all << separator();
        }
        all += buildSectionMenu(section, s);
    }
    return all;
}

}
