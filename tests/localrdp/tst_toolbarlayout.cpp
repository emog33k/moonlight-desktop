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
    QCOMPARE(left, QStringList({ "Мышь", "Клавиатура", "Буфер", "Экран", "Звук", "Действия", "ПК" }));
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

void TestToolbarLayout::compactForFullScreen()
{
    // Full screen: only as wide as its buttons (then centred by the window), so the
    // screen's top-right corner is not a Disconnect button
    const QVector<ToolbarButton> b = toolbarButtons();
    const ToolbarLayout wide = layoutToolbar(b, 1920, 1.0, fixedWidth);
    const ToolbarLayout compact = layoutToolbar(b, 1920, 1.0, fixedWidth, true);
    QVERIFY(compact.size.width() < 1000);
    QCOMPARE(compact.size.height(), wide.size.height());
    for (int i = 0; i < b.size(); i++) {
        QVERIFY(!compact.rects[i].isEmpty());
        QVERIFY(compact.rects[i].right() < compact.size.width());
    }
    QCOMPARE(compact.rects.last().right(), compact.size.width() - 1);
    // Never wider than the screen
    QVERIFY(layoutToolbar(b, 300, 1.0, fixedWidth, true).size.width() <= 300);
}

void TestToolbarLayout::monitorButtonsNumberedLikeWindows()
{
    // Sunshine's order (index) need not match Windows' numbers (\\.\DISPLAYn)
    QVector<DisplayInfo> displays = { { QString(R"(\\.\DISPLAY2)"), 1920, 1080, false },
                                      { QString(R"(\\.\DISPLAY1)"), 2560, 1440, true } };
    const QVector<ToolbarButton> b = toolbarButtons(displays, 0);
    QVector<ToolbarButton> monitors;
    for (const ToolbarButton& x : b) {
        if (x.kind == ToolbarButton::Kind::Action && x.action == StreamAction::SwitchDisplay) monitors << x;
    }
    QCOMPARE(monitors.size(), 2);
    QCOMPARE(monitors[0].label, QString("1"));
    QCOMPARE(monitors[0].param, 1); // Windows' 1 is Sunshine's second
    QCOMPARE(monitors[0].selected, false);
    QCOMPARE(monitors[1].label, QString("2"));
    QCOMPARE(monitors[1].param, 0);
    QCOMPARE(monitors[1].selected, true);
    QVERIFY(!monitors[0].rightSide);
    // After the sections, so their positions never change
    QCOMPARE(b.indexOf(monitors[0]), toolbarButtons().size() - 3);
    // One monitor (or none known): no buttons
    QCOMPARE(toolbarButtons({ displays[0] }, 0).size(), toolbarButtons().size());
}

void TestToolbarLayout::windowsNumberFromName()
{
    QCOMPARE(windowsDisplayNumber(QString(R"(\\.\DISPLAY3)"), 0), 3);
    QCOMPARE(windowsDisplayNumber(QString(R"(\\.\display12)"), 0), 12);
    QCOMPARE(windowsDisplayNumber(QString("something"), 4), 5); // fallback: position + 1
    QCOMPARE(windowsDisplayNumber(QString(), 0), 1);
}
