#include "tests.h"
#include "streaming/localrdp/menulayout.h"

#include <QTest>

using namespace LocalRdp;

static QVector<MenuItem> sampleItems()
{
    return {
        { MenuItemKind::Header, "Мышь", "", std::nullopt, false },
        { MenuItemKind::Radio, "A", "", StreamAction::SetDesktopMouseMode, true },
        { MenuItemKind::Separator, "", "", std::nullopt, false },
        { MenuItemKind::Command, "B", "Ctrl+Alt+Shift+Q", StreamAction::Disconnect, false },
    };
}

void TestMenuLayout::rowsStackAtScale1()
{
    const MenuLayout l = layoutMenu(sampleItems(), 1.0);
    // padding 6, header 26, item 30, separator 9, item 30, padding 6
    QCOMPARE(l.itemRects.size(), 4);
    QCOMPARE(l.itemRects[0], QRect(6, 6, 368, 26));
    QCOMPARE(l.itemRects[1], QRect(6, 32, 368, 30));
    QCOMPARE(l.itemRects[2], QRect(6, 62, 368, 9));
    QCOMPARE(l.itemRects[3], QRect(6, 71, 368, 30));
    QCOMPARE(l.size, QSize(380, 107));
}

void TestMenuLayout::scalesWithDpi()
{
    const MenuLayout l = layoutMenu(sampleItems(), 1.5);
    QCOMPARE(l.size, QSize(570, 161)); // every unit scaled by 1.5, rounded
    QCOMPARE(l.itemRects[1].height(), 45);
}

void TestMenuLayout::hitTestOnlyActionable()
{
    const QVector<MenuItem> items = sampleItems();
    const MenuLayout l = layoutMenu(items, 1.0);
    QCOMPARE(hitTestMenu(l, items, QPoint(50, 10)), -1);  // header
    QCOMPARE(hitTestMenu(l, items, QPoint(50, 40)), 1);   // radio
    QCOMPARE(hitTestMenu(l, items, QPoint(50, 65)), -1);  // separator
    QCOMPARE(hitTestMenu(l, items, QPoint(50, 80)), 3);   // command
    QCOMPARE(hitTestMenu(l, items, QPoint(500, 80)), -1); // outside
    QCOMPARE(hitTestMenu(l, items, QPoint(2, 40)), -1);   // padding
}

void TestMenuLayout::keyboardNavigationSkipsNonActionable()
{
    const QVector<MenuItem> items = sampleItems();
    QCOMPARE(nextActionable(items, -1, +1), 1);
    QCOMPARE(nextActionable(items, 1, +1), 3);
    QCOMPARE(nextActionable(items, 3, +1), 1); // wraps
    QCOMPARE(nextActionable(items, 1, -1), 3); // wraps backwards
    QCOMPARE(nextActionable(items, -1, -1), 3);
}

void TestMenuLayout::menuLeftOfRightEdgeButton()
{
    const QRect work(0, 0, 1920, 1040);
    const QRect button(1800, 400, 40, 40);
    QCOMPARE(placeMenu(button, Edge::Right, QSize(310, 500), work, 6), QPoint(1800 - 6 - 310, 400));
}

void TestMenuLayout::menuRightOfLeftEdgeButton()
{
    const QRect work(0, 0, 1920, 1040);
    const QRect button(20, 400, 40, 40);
    QCOMPARE(placeMenu(button, Edge::Left, QSize(310, 500), work, 6), QPoint(20 + 40 + 6, 400));
}

void TestMenuLayout::menuBelowTopEdgeButton()
{
    const QRect work(0, 0, 1920, 1040);
    const QRect button(900, 10, 40, 40);
    QCOMPARE(placeMenu(button, Edge::Top, QSize(310, 500), work, 6), QPoint(900, 10 + 40 + 6));
}

void TestMenuLayout::menuAboveBottomEdgeButton()
{
    const QRect work(0, 0, 1920, 1040);
    const QRect button(900, 990, 40, 40);
    QCOMPARE(placeMenu(button, Edge::Bottom, QSize(310, 500), work, 6), QPoint(900, 990 - 6 - 500));
}

void TestMenuLayout::menuKeptInsideWorkArea()
{
    const QRect work(0, 0, 1920, 1040);
    // Button low on the right edge: the menu would run past the bottom
    QCOMPARE(placeMenu(QRect(1800, 900, 40, 40), Edge::Right, QSize(310, 500), work, 6),
             QPoint(1484, 1040 - 500));
    // Button near the top-right corner on the top edge: pushed left
    QCOMPARE(placeMenu(QRect(1860, 10, 40, 40), Edge::Top, QSize(310, 500), work, 6),
             QPoint(1920 - 310, 56));
    // Work area with an offset (second monitor)
    QCOMPARE(placeMenu(QRect(1930, 5, 40, 40), Edge::Left, QSize(310, 500), QRect(1920, 0, 1280, 1024), 6),
             QPoint(1976, 5));
}
