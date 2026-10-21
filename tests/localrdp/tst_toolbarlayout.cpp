#include "tests.h"
#include "streaming/localrdp/toolbarlayout.h"

#include <QTest>

using namespace LocalRdp;

static int fixedWidth(const QString& s) { return s.size() * 7; }

void TestToolbarLayout::buttonsAreSectionsThenActions()
{
    const QVector<ToolbarButton> b = toolbarButtons();
    QStringList left;
    QVector<StreamAction> right;
    for (const ToolbarButton& x : b) {
        if (x.rightSide) right << x.action;
        else left << x.label;
    }
    QCOMPARE(left, QStringList({ "Мышь", "Клавиатура", "Буфер", "Экран", "ПК" }));
    QCOMPARE(right, QVector<StreamAction>({ StreamAction::ToggleFullscreen, StreamAction::Minimize, StreamAction::Disconnect }));
}

void TestToolbarLayout::leftToRightAndRightAligned()
{
    const QVector<ToolbarButton> b = toolbarButtons();
    const ToolbarLayout l = layoutToolbar(b, 1280, 1.0, fixedWidth);
    QCOMPARE(l.size, QSize(1280, kToolbarBaseHeight));
    QCOMPARE(l.rects.size(), b.size());
    int lastLeftEnd = 0;
    for (int i = 0; i < b.size(); i++) {
        QVERIFY(!l.rects[i].isEmpty());
        QCOMPARE(l.rects[i].top(), 0);
        QCOMPARE(l.rects[i].height(), kToolbarBaseHeight);
        if (!b[i].rightSide) {
            QVERIFY(l.rects[i].left() >= lastLeftEnd);
            lastLeftEnd = l.rects[i].right();
        }
    }
    // The last right-side button ends at the right edge
    QCOMPARE(l.rects.last().right(), 1280 - 1);
}

void TestToolbarLayout::narrowWindowKeepsRightButtons()
{
    const QVector<ToolbarButton> b = toolbarButtons();
    const ToolbarLayout l = layoutToolbar(b, 300, 1.0, fixedWidth);
    for (int i = 0; i < b.size(); i++) {
        if (b[i].rightSide) QVERIFY2(!l.rects[i].isEmpty(), "disconnect must always be reachable");
    }
    // Left buttons that don't fit are dropped, never overlap the right ones
    int firstRight = 300;
    for (int i = 0; i < b.size(); i++) if (b[i].rightSide) firstRight = qMin(firstRight, l.rects[i].left());
    for (int i = 0; i < b.size(); i++) if (!b[i].rightSide && !l.rects[i].isEmpty()) QVERIFY(l.rects[i].right() < firstRight);
}

void TestToolbarLayout::scalesWithDpi()
{
    const ToolbarLayout l = layoutToolbar(toolbarButtons(), 1920, 1.5, fixedWidth);
    QCOMPARE(l.size.height(), 51);
}

void TestToolbarLayout::hitTest()
{
    const QVector<ToolbarButton> b = toolbarButtons();
    const ToolbarLayout l = layoutToolbar(b, 1280, 1.0, fixedWidth);
    QCOMPARE(hitTestToolbar(l, l.rects[0].center()), 0);
    QCOMPARE(hitTestToolbar(l, l.rects.last().center()), b.size() - 1);
    // Gap between the groups
    QCOMPARE(hitTestToolbar(l, QPoint(640, 10)), -1);
    QCOMPARE(hitTestToolbar(l, QPoint(10, 200)), -1);
}
