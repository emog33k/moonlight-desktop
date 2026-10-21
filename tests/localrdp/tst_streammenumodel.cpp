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

void TestStreamMenuModel::headersInOrder()
{
    QStringList headers;
    for (const MenuItem& item : buildStreamMenu(StreamState())) {
        if (item.kind == MenuItemKind::Header) {
            headers << item.label;
        }
    }
    QCOMPARE(headers, QStringList({ "Мышь", "Клавиатура", "Буфер обмена", "Экран", "ПК" }));
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
        { &StreamState::localCursorVisible, StreamAction::ToggleLocalCursor },
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
    QVector<int> counts(static_cast<int>(StreamAction::Count), 0);
    for (const MenuItem& item : buildStreamMenu(StreamState())) {
        if (item.action) {
            counts[static_cast<int>(*item.action)]++;
        }
    }
    for (int i = 0; i < counts.size(); i++) {
        QVERIFY2(counts[i] == 1, qPrintable(QString("action %1 appears %2 times").arg(i).arg(counts[i])));
    }
}

void TestStreamMenuModel::shortcutText()
{
    const QVector<MenuItem> items = buildStreamMenu(StreamState());
    QCOMPARE(findByAction(items, StreamAction::SetGameMouseMode)->shortcut, QString("Ctrl+Alt+Shift+M"));
    QCOMPARE(findByAction(items, StreamAction::SetDesktopMouseMode)->shortcut, QString());
    QCOMPARE(findByAction(items, StreamAction::Disconnect)->shortcut, QString("Ctrl+Alt+Shift+Q"));
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
    // The preference can be changed any time; it is sent when the agent connects
    state.agentStatus = AgentStatus::NotFound;
    QCOMPARE(findByAction(buildStreamMenu(state), StreamAction::ToggleLockOnDisconnect)->enabled, true);
}

void TestStreamMenuModel::lockPcTexts()
{
    const QVector<MenuItem> items = buildStreamMenu(StreamState());
    QCOMPARE(findByAction(items, StreamAction::LockPc)->label, QString("Заблокировать ПК"));
    QCOMPARE(findByAction(items, StreamAction::ToggleLockOnDisconnect)->label, QString("Блокировать ПК при отключении"));
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
