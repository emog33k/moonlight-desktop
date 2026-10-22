#include "tests.h"
#include "streaming/localrdp/streammenumodel.h"

#include <QSet>
#include <QTest>

using namespace LocalRdp;

static const MenuItem* findByLabel(const QVector<MenuItem>& items, const QString& label)
{
    for (const MenuItem& item : items) {
        if (item.label == label) {
            return &item;
        }
    }
    return nullptr;
}

static const MenuItem* findByAction(const QVector<MenuItem>& items, StreamAction action)
{
    for (const MenuItem& item : items) {
        if (item.action && *item.action == action) {
            return &item;
        }
    }
    return nullptr;
}

static QVector<StreamAction> actionsOf(const QVector<MenuItem>& items)
{
    QVector<StreamAction> out;
    for (const MenuItem& item : items) {
        if (item.action) out << *item.action;
    }
    return out;
}

void TestStreamMenuModel::headersInOrder()
{
    QStringList headers;
    for (const MenuItem& item : buildStreamMenu(StreamState())) {
        if (item.kind == MenuItemKind::Header) {
            headers << item.label;
        }
    }
    QCOMPARE(headers, QStringList({ "Мышь", "Клавиатура", "Буфер обмена", "Монитор ПК", "Экран",
                                    "Снимок экрана", "Буфер обмена", "Клавиши на ПК", "ПК" }));
}

void TestStreamMenuModel::desktopModeChecked()
{
    StreamState state;
    state.absoluteMouse = true;
    QVector<MenuItem> items = buildStreamMenu(state);
    QCOMPARE(findByLabel(items, "Режим рабочего стола")->checked, true);
    QCOMPARE(findByLabel(items, "Игровой режим")->checked, false);

    state.absoluteMouse = false;
    items = buildStreamMenu(state);
    QCOMPARE(findByLabel(items, "Режим рабочего стола")->checked, false);
    QCOMPARE(findByLabel(items, "Игровой режим")->checked, true);
}

void TestStreamMenuModel::checksMirrorState()
{
    struct Case { bool StreamState::* field; StreamAction action; };
    const Case cases[] = {
        { &StreamState::localCursorShapes, StreamAction::ToggleLocalCursorShapes },
        { &StreamState::pointerLocked, StreamAction::TogglePointerLock },
        { &StreamState::systemKeyCapture, StreamAction::ToggleSystemKeyCapture },
        { &StreamState::fullscreen, StreamAction::ToggleFullscreen },
        { &StreamState::statsVisible, StreamAction::ToggleStats },
        { &StreamState::lockOnDisconnect, StreamAction::ToggleLockOnDisconnect },
        { &StreamState::clipboardSync, StreamAction::ToggleClipboardSync },
    };

    for (const Case& c : cases) {
        for (bool value : { false, true }) {
            StreamState state;
            state.*(c.field) = value;
            const MenuItem* item = findByAction(buildStreamMenu(state), c.action);
            QVERIFY(item != nullptr);
            QCOMPARE(item->kind, MenuItemKind::Check);
            QCOMPARE(item->checked, value);
        }
    }
}

void TestStreamMenuModel::everyActionableItemHasAction()
{
    for (const MenuItem& item : buildStreamMenu(StreamState())) {
        const bool actionable = item.kind == MenuItemKind::Radio ||
                                item.kind == MenuItemKind::Check ||
                                item.kind == MenuItemKind::Command;
        QCOMPARE(item.action.has_value(), actionable);
    }
}

void TestStreamMenuModel::everyActionAppearsOnce()
{
    // Hotkey-only actions are not in the menu; SwitchDisplay appears once per
    // monitor, SendKeys once per quick key combination.
    const QSet<StreamAction> notInMenu = { StreamAction::ToggleLocalCursor };
    QVector<int> counts(static_cast<int>(StreamAction::Count), 0);
    for (const MenuItem& item : buildStreamMenu(StreamState())) {
        if (item.action) {
            counts[static_cast<int>(*item.action)]++;
        }
    }
    for (int i = 0; i < counts.size(); i++) {
        const StreamAction a = static_cast<StreamAction>(i);
        const int expected = notInMenu.contains(a) ? 0
                           : a == StreamAction::SwitchDisplay ? 4
                           : a == StreamAction::SendKeys ? (int)QuickKeys::Count
                           : 1;
        QVERIFY2(counts[i] == expected, qPrintable(QString("action %1 appears %2 times").arg(i).arg(counts[i])));
    }
}

void TestStreamMenuModel::shortcutText()
{
    const QVector<MenuItem> items = buildStreamMenu(StreamState());
    QCOMPARE(findByAction(items, StreamAction::SetGameMouseMode)->shortcut, QString("Ctrl+Alt+Shift+M"));
    QCOMPARE(findByAction(items, StreamAction::SetDesktopMouseMode)->shortcut, QString());
    QCOMPARE(findByAction(items, StreamAction::Disconnect)->shortcut, QString("Ctrl+Alt+Shift+Q"));
    QVERIFY(!findByLabel(items, "Курсор ПК: показать/скрыть")); // replaced by AnyDesk-style cursor
}

void TestStreamMenuModel::agentStatusLine()
{
    struct Case { AgentStatus status; QString text; };
    const Case cases[] = {
        { AgentStatus::Unknown, "Агент на ПК: —" },
        { AgentStatus::Connecting, "Агент на ПК: подключение…" },
        { AgentStatus::Connected, "Агент на ПК: подключён" },
        { AgentStatus::NotFound, "Агент на ПК: не найден" },
        { AgentStatus::CertError, "Агент на ПК: ошибка сертификата" },
        { AgentStatus::Rejected, "Агент на ПК: ПК отклонил подключение" },
    };
    for (const Case& c : cases) {
        StreamState state;
        state.agentStatus = c.status;
        int statusLines = 0;
        for (const MenuItem& item : buildStreamMenu(state)) {
            if (item.kind == MenuItemKind::Status) {
                statusLines++;
                QCOMPARE(item.label, c.text);
                QVERIFY(!item.action.has_value());
            }
        }
        QCOMPARE(statusLines, 1);
    }
}

void TestStreamMenuModel::lockPcNeedsAgent()
{
    StreamState state;
    for (AgentStatus status : { AgentStatus::Unknown, AgentStatus::Connecting, AgentStatus::NotFound,
                                AgentStatus::CertError, AgentStatus::Rejected }) {
        state.agentStatus = status;
        QCOMPARE(findByAction(buildStreamMenu(state), StreamAction::LockPc)->enabled, false);
    }
    state.agentStatus = AgentStatus::Connected;
    QCOMPARE(findByAction(buildStreamMenu(state), StreamAction::LockPc)->enabled, true);
    state.agentStatus = AgentStatus::NotFound;
    QCOMPARE(findByAction(buildStreamMenu(state), StreamAction::ToggleLockOnDisconnect)->enabled, true);
}

void TestStreamMenuModel::lockPcTexts()
{
    const QVector<MenuItem> items = buildStreamMenu(StreamState());
    QCOMPARE(findByAction(items, StreamAction::LockPc)->label, QString("Заблокировать ПК"));
    QCOMPARE(findByAction(items, StreamAction::ToggleLockOnDisconnect)->label, QString("Блокировать ПК при отключении"));
    QCOMPARE(findByAction(items, StreamAction::ToggleLocalCursorShapes)->label, QString("Курсор как в AnyDesk"));
}

void TestStreamMenuModel::sendFilesNeedsAgentAndSync()
{
    StreamState state;
    state.agentStatus = AgentStatus::Connected;
    state.clipboardSync = true;
    QCOMPARE(findByAction(buildStreamMenu(state), StreamAction::SendFilesToPc)->enabled, true);
    state.clipboardSync = false;
    QCOMPARE(findByAction(buildStreamMenu(state), StreamAction::SendFilesToPc)->enabled, false);
    state.clipboardSync = true;
    state.agentStatus = AgentStatus::NotFound;
    QCOMPARE(findByAction(buildStreamMenu(state), StreamAction::SendFilesToPc)->enabled, false);
    QCOMPARE(findByAction(buildStreamMenu(state), StreamAction::SendFilesToPc)->label, QString("Отправить файлы на ПК…"));
    QCOMPARE(findByAction(buildStreamMenu(state), StreamAction::ToggleClipboardSync)->label, QString("Синхронизация буфера обмена"));
}

void TestStreamMenuModel::sectionsSplit()
{
    const StreamState s;
    QCOMPARE(actionsOf(buildSectionMenu(MenuSection::Mouse, s)),
             QVector<StreamAction>({ StreamAction::SetDesktopMouseMode, StreamAction::SetGameMouseMode,
                                     StreamAction::ToggleLocalCursorShapes, StreamAction::TogglePointerLock }));
    QCOMPARE(actionsOf(buildSectionMenu(MenuSection::Keyboard, s)),
             QVector<StreamAction>({ StreamAction::ToggleSystemKeyCapture, StreamAction::PasteClipboardText }));
    QCOMPARE(actionsOf(buildSectionMenu(MenuSection::Clipboard, s)),
             QVector<StreamAction>({ StreamAction::ToggleClipboardSync, StreamAction::SendFilesToPc }));
    QCOMPARE(actionsOf(buildSectionMenu(MenuSection::Pc, s)),
             QVector<StreamAction>({ StreamAction::LockPc, StreamAction::ToggleLockOnDisconnect }));
    QCOMPARE(actionsOf(buildSectionMenu(MenuSection::Session, s)),
             QVector<StreamAction>({ StreamAction::Minimize, StreamAction::Disconnect, StreamAction::DisconnectAndExit }));
    const QVector<StreamAction> screen = actionsOf(buildSectionMenu(MenuSection::Screen, s));
    QVERIFY(screen.contains(StreamAction::ToggleFullscreen));
    QVERIFY(screen.contains(StreamAction::ToggleStats));
    QVERIFY(screen.contains(StreamAction::SwitchDisplay));
}

void TestStreamMenuModel::displaysListed()
{
    StreamState s;
    s.displays = { { "DISPLAY1", 2560, 1440, true }, { "DISPLAY2", 1920, 1080, false } };
    s.currentDisplay = 1;
    QVector<MenuItem> radios;
    for (const MenuItem& item : buildSectionMenu(MenuSection::Screen, s)) {
        if (item.action && *item.action == StreamAction::SwitchDisplay) radios << item;
    }
    QCOMPARE(radios.size(), 2);
    QCOMPARE(radios[0].label, QString("Монитор 1: 2560×1440, основной"));
    QCOMPARE(radios[1].label, QString("Монитор 2: 1920×1080"));
    QCOMPARE(radios[0].param, 0);
    QCOMPARE(radios[1].param, 1);
    QCOMPARE(radios[0].checked, false);
    QCOMPARE(radios[1].checked, true);
    QCOMPARE(radios[0].kind, MenuItemKind::Radio);
    QCOMPARE(radios[0].shortcut, QString("Ctrl+Alt+Shift+F1"));
}

void TestStreamMenuModel::displaysUnknownGivesGeneric()
{
    QStringList labels;
    for (const MenuItem& item : buildSectionMenu(MenuSection::Screen, StreamState())) {
        if (item.action && *item.action == StreamAction::SwitchDisplay) {
            labels << item.label;
            QCOMPARE(item.checked, false);
        }
    }
    QCOMPARE(labels, QStringList({ "Монитор 1", "Монитор 2", "Монитор 3", "Монитор 4" }));
}

void TestStreamMenuModel::toolbarPcMenuEndsWithSession()
{
    // The toolbar has no "session" dropdown: its items sit at the end of "ПК"
    const StreamState s;
    const QVector<MenuItem> pc = buildToolbarMenu(MenuSection::Pc, s);
    QCOMPARE(actionsOf(pc), QVector<StreamAction>({ StreamAction::LockPc, StreamAction::ToggleLockOnDisconnect,
                                                    StreamAction::Minimize, StreamAction::Disconnect,
                                                    StreamAction::DisconnectAndExit }));
    QCOMPARE(actionsOf(buildToolbarMenu(MenuSection::Mouse, s)), actionsOf(buildSectionMenu(MenuSection::Mouse, s)));
}

void TestStreamMenuModel::actionsSection()
{
    StreamState s;
    s.agentStatus = AgentStatus::Connected;
    s.clipboardSync = true;
    const QVector<MenuItem> items = buildSectionMenu(MenuSection::Actions, s);
    QVector<QPair<StreamAction, int>> got;
    for (const MenuItem& i : items) {
        if (i.action) {
            got << qMakePair(*i.action, i.param);
            QVERIFY2(i.enabled, qPrintable(i.label));
        }
    }
    const QVector<QPair<StreamAction, int>> want = {
        { StreamAction::SnipLaptop, 0 },
        { StreamAction::SendKeys, (int)QuickKeys::SnipArea },
        { StreamAction::SendKeys, (int)QuickKeys::PrintScreen },
        { StreamAction::SendKeys, (int)QuickKeys::Paste },
        { StreamAction::SendKeys, (int)QuickKeys::TaskManager },
        { StreamAction::SendKeys, (int)QuickKeys::ShowDesktop },
        { StreamAction::SendKeys, (int)QuickKeys::Explorer },
        { StreamAction::SendKeys, (int)QuickKeys::Start },
        { StreamAction::SendKeys, (int)QuickKeys::AltTab },
    };
    QCOMPARE(got, want);
}

void TestStreamMenuModel::screenshotsNeedClipboardSync()
{
    // Screenshots travel through the synced clipboard
    StreamState s;
    s.agentStatus = AgentStatus::NotFound;
    s.clipboardSync = true;
    QVector<MenuItem> items = buildSectionMenu(MenuSection::Actions, s);
    QCOMPARE(findByAction(items, StreamAction::SnipLaptop)->enabled, false);
    s.agentStatus = AgentStatus::Connected;
    s.clipboardSync = false;
    items = buildSectionMenu(MenuSection::Actions, s);
    QCOMPARE(findByAction(items, StreamAction::SnipLaptop)->enabled, false);
    // Keys always work
    for (const MenuItem& i : items) {
        if (i.action && *i.action == StreamAction::SendKeys && i.param == (int)QuickKeys::TaskManager) {
            QVERIFY(i.enabled);
        }
    }
}

void TestStreamMenuModel::quickKeyCombos()
{
    using V = QVector<short>;
    QCOMPARE(quickKeyCombo(QuickKeys::Paste), V({ 0xA2, 0x56 }));             // Ctrl+V
    QCOMPARE(quickKeyCombo(QuickKeys::SnipArea), V({ 0x5B, 0xA0, 0x53 }));    // Win+Shift+S
    QCOMPARE(quickKeyCombo(QuickKeys::PrintScreen), V({ 0x2C }));
    QCOMPARE(quickKeyCombo(QuickKeys::TaskManager), V({ 0xA2, 0xA0, 0x1B })); // Ctrl+Shift+Esc
    QCOMPARE(quickKeyCombo(QuickKeys::ShowDesktop), V({ 0x5B, 0x44 }));       // Win+D
    QCOMPARE(quickKeyCombo(QuickKeys::Explorer), V({ 0x5B, 0x45 }));          // Win+E
    QCOMPARE(quickKeyCombo(QuickKeys::Start), V({ 0x5B }));
    QCOMPARE(quickKeyCombo(QuickKeys::AltTab), V({ 0xA4, 0x09 }));
    QVERIFY(quickKeyCombo((QuickKeys)99).isEmpty());
}
