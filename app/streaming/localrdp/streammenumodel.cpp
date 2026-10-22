#include "streammenumodel.h"
#include "localrdp-shared/desktoptr.h"

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
        return dtr("Host agent: connecting…");
    case AgentStatus::Connected:
        return dtr("Host agent: connected");
    case AgentStatus::NotFound:
        return dtr("Host agent: not found");
    case AgentStatus::CertError:
        return dtr("Host agent: certificate error");
    case AgentStatus::Rejected:
        return dtr("Host agent: the host refused the connection");
    case AgentStatus::Unknown:
        break;
    }
    return dtr("Host agent: —");
}

QVector<QualityPreset> qualityPresets()
{
    return {
        { dtr("Low (slow network)"), 8000, 30 },
        { dtr("Normal"), 20000, 60 },
        { dtr("Maximum"), 60000, 60 },
    };
}

int windowsDisplayNumber(const QString& deviceName, int position)
{
    static const QRegularExpression number(QStringLiteral("DISPLAY(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = number.match(deviceName);
    const int n = m.hasMatch() ? m.captured(1).toInt() : 0;
    return n > 0 ? n : position + 1;
}

QVector<int> displaysInWindowsOrder(const QVector<DisplayInfo>& displays)
{
    QVector<int> order;
    for (int i = 0; i < qMin(displays.size(), 12); i++) {
        order << i;
    }
    std::stable_sort(order.begin(), order.end(), [&displays](int a, int b) {
        const DisplayInfo& da = displays[a];
        const DisplayInfo& db = displays[b];
        if (da.x != db.x) {
            return da.x < db.x;
        }
        if (da.y != db.y) {
            return da.y < db.y;
        }
        return windowsDisplayNumber(da.name, a) < windowsDisplayNumber(db.name, b);
    });
    return order;
}

static QVector<MenuItem> displayItems(const StreamState& s)
{
    QVector<MenuItem> items;
    // Sunshine switches the captured monitor on Ctrl+Alt+Shift+F1..F12 (its own
    // order); the menu shows Windows' numbers, in the order the monitors stand.
    if (s.displays.isEmpty()) {
        for (int i = 0; i < 4; i++) {
            MenuItem m = item(MenuItemKind::Radio, dtr("Display %1").arg(i + 1),
                              QStringLiteral("Ctrl+Alt+Shift+F%1").arg(i + 1), StreamAction::SwitchDisplay, false);
            m.param = i;
            items << m;
        }
        return items;
    }
    for (int i : displaysInWindowsOrder(s.displays)) {
        const DisplayInfo& d = s.displays[i];
        QString label = dtr("Display %1: %2×%3").arg(windowsDisplayNumber(d.name, i)).arg(d.width).arg(d.height);
        if (d.primary) {
            label += dtr(", primary");
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
            header(dtr("Mouse")),
            item(K::Radio, dtr("Desktop mode"), QString(), A::SetDesktopMouseMode, s.absoluteMouse),
            item(K::Radio, dtr("Game mode"), QStringLiteral("Ctrl+Alt+Shift+M"), A::SetGameMouseMode, !s.absoluteMouse),
            item(K::Check, dtr("Host cursor shapes"), QString(), A::ToggleLocalCursorShapes, s.localCursorShapes),
            item(K::Check, dtr("Keep the cursor in the window"), QStringLiteral("Ctrl+Alt+Shift+L"), A::TogglePointerLock, s.pointerLocked),
        };
        if (s.cursorSyncActive) {
            items << item(K::Command, dtr("Two cursors visible — hide the extra one"), QString(), A::FixDoubleCursor);
        }
        return items;
    }
    case MenuSection::Keyboard:
        return {
            header(dtr("Keyboard")),
            item(K::Check, dtr("Send Alt+Tab and Win to the host"), QStringLiteral("Ctrl+Alt+Shift+K"), A::ToggleSystemKeyCapture, s.systemKeyCapture),
            item(K::Command, dtr("Type the text from this computer's clipboard"), QStringLiteral("Ctrl+Alt+Shift+V"), A::PasteClipboardText),
        };
    case MenuSection::Clipboard:
        return {
            header(dtr("Clipboard")),
            item(K::Check, dtr("Clipboard sync"), QString(), A::ToggleClipboardSync, s.clipboardSync),
            disabledUnless(item(K::Command, dtr("Send files to the host…"), QString(), A::SendFilesToPc),
                           s.clipboardSync && s.agentStatus == AgentStatus::Connected),
            status(dtr("Or drag files onto the stream window")),
        };
    case MenuSection::Screen:
    {
        QVector<MenuItem> items = { header(dtr("Host display")) };
        items += displayItems(s);
        items += {
            header(dtr("Screen")),
            item(K::Check, dtr("Full screen"), QStringLiteral("Ctrl+Alt+Shift+X"), A::ToggleFullscreen, s.fullscreen),
            item(K::Check, dtr("Statistics"), QStringLiteral("Ctrl+Alt+Shift+S"), A::ToggleStats, s.statsVisible),
            header(dtr("Quality (the stream reconnects)")),
        };
        const QVector<QualityPreset> presets = qualityPresets();
        for (int i = 0; i < presets.size(); i++) {
            const QualityPreset& p = presets[i];
            MenuItem m = item(K::Radio, dtr("%1: %2 Mbit/s, %3 fps").arg(p.name).arg(p.kbps / 1000).arg(p.fps),
                              QString(), A::SetQuality, s.bitrateKbps == p.kbps && s.fps == p.fps);
            m.param = i;
            items << m;
        }
        return items;
    }
    case MenuSection::Sound:
    {
        MenuItem onOff = item(K::Check, dtr("Sound on"), QString(), A::ToggleStreamAudio, s.streamAudio);
        MenuItem volume = item(K::Slider, dtr("Volume"), QString(), A::SetVolume);
        volume.param = s.streamVolume;
        onOff.keepOpen = volume.keepOpen = true;
        return {
            header(dtr("Host sound")),
            onOff,
            volume,
        };
    }
    case MenuSection::Actions:
    {
        // Screenshots travel through the synced clipboard
        const bool viaClipboard = s.clipboardSync && s.agentStatus == AgentStatus::Connected;
        return {
            header(dtr("Screenshot")),
            disabledUnless(item(K::Command, dtr("Area on this computer → host clipboard"), QString(), A::SnipLaptop),
                           viaClipboard),
            disabledUnless(keys(dtr("Area on the host → clipboard here"), QuickKeys::SnipArea, QStringLiteral("Win+Shift+S")),
                           viaClipboard),
            disabledUnless(keys(dtr("Whole host screen → clipboard here"), QuickKeys::PrintScreen, QStringLiteral("PrtSc")),
                           viaClipboard),
            header(dtr("Clipboard")),
            keys(dtr("Paste on the host"), QuickKeys::Paste, QStringLiteral("Ctrl+V")),
            header(dtr("Keys on the host")),
            keys(dtr("Task Manager"), QuickKeys::TaskManager, QStringLiteral("Ctrl+Shift+Esc")),
            keys(dtr("Show desktop"), QuickKeys::ShowDesktop, QStringLiteral("Win+D")),
            keys(dtr("File Explorer"), QuickKeys::Explorer, QStringLiteral("Win+E")),
            keys(dtr("Start menu"), QuickKeys::Start, QStringLiteral("Win")),
            keys(dtr("Switch window"), QuickKeys::AltTab, QStringLiteral("Alt+Tab")),
        };
    }
    case MenuSection::Pc:
        return {
            header(dtr("Host")),
            status(agentStatusText(s.agentStatus) +
                   (s.agentStatus == AgentStatus::Connected && s.pcLocked ? dtr(", host locked") : QString())),
            disabledUnless(item(K::Command, dtr("Lock the host"), QString(), A::LockPc),
                           s.agentStatus == AgentStatus::Connected),
            item(K::Check, dtr("Lock the host on disconnect"), QString(), A::ToggleLockOnDisconnect, s.lockOnDisconnect),
            power(dtr("Restart the host…"), dtr("Click again to restart the host"),
                  A::RebootPc, s.powerConfirm == PowerConfirm::Reboot, s.agentStatus == AgentStatus::Connected),
            power(dtr("Shut down the host…"), dtr("Click again to shut down the host"),
                  A::ShutdownPc, s.powerConfirm == PowerConfirm::Shutdown, s.agentStatus == AgentStatus::Connected),
        };
    case MenuSection::Session:
        return {
            item(K::Command, dtr("Minimize"), QStringLiteral("Ctrl+Alt+Shift+D"), A::Minimize),
            item(K::Command, dtr("Disconnect"), QStringLiteral("Ctrl+Alt+Shift+Q"), A::Disconnect),
            item(K::Command, dtr("Disconnect and quit Moonlight"), QStringLiteral("Ctrl+Alt+Shift+E"), A::DisconnectAndExit),
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
