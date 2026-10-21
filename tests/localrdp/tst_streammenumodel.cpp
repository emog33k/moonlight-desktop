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
    QCOMPARE(headers, QStringList({ "Мышь", "Клавиатура", "Экран" }));
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
