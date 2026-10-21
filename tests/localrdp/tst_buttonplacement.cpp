#include "tests.h"
#include "streaming/localrdp/buttonplacement.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace LocalRdp;

void TestButtonPlacement::rightEdgeMiddle()
{
    QCOMPARE(placeButton(QRect(100, 100, 1000, 600), 40, 8, { Edge::Right, 0.5 }), QPoint(1052, 380));
}

void TestButtonPlacement::leftEdgeTop()
{
    QCOMPARE(placeButton(QRect(100, 100, 1000, 600), 40, 8, { Edge::Left, 0.0 }), QPoint(108, 100));
}

void TestButtonPlacement::bottomEdgeEnd()
{
    QCOMPARE(placeButton(QRect(0, 0, 800, 600), 40, 8, { Edge::Bottom, 1.0 }), QPoint(760, 552));
}

void TestButtonPlacement::topEdgeStart()
{
    QCOMPARE(placeButton(QRect(0, 0, 800, 600), 40, 8, { Edge::Top, 0.0 }), QPoint(0, 8));
}

void TestButtonPlacement::fractionClamped()
{
    const QRect client(100, 100, 1000, 600);
    QCOMPARE(placeButton(client, 40, 8, { Edge::Right, 1.7 }), placeButton(client, 40, 8, { Edge::Right, 1.0 }));
    QCOMPARE(placeButton(client, 40, 8, { Edge::Right, -0.3 }), placeButton(client, 40, 8, { Edge::Right, 0.0 }));
}

void TestButtonPlacement::tinyWindow()
{
    QCOMPARE(placeButton(QRect(0, 0, 30, 30), 40, 8, { Edge::Right, 0.5 }), QPoint(0, 0));
}

void TestButtonPlacement::snapRight()
{
    const ButtonPlacement p = snapToNearestEdge(QRect(100, 100, 1000, 600), QPoint(1090, 200), 40);
    QCOMPARE(p.edge, Edge::Right);
    QVERIFY(qFuzzyCompare(p.fraction, 80.0 / 560.0));
}

void TestButtonPlacement::snapTop()
{
    QCOMPARE(snapToNearestEdge(QRect(100, 100, 1000, 600), QPoint(500, 112), 40).edge, Edge::Top);
}

void TestButtonPlacement::snapRoundTrip()
{
    // Snapping where placeButton put the button gives the same placement back.
    const QRect client(0, 0, 1280, 720);
    const ButtonPlacement original { Edge::Left, 0.25 };
    const QPoint topLeft = placeButton(client, 40, 8, original);
    const ButtonPlacement snapped = snapToNearestEdge(client, topLeft + QPoint(20, 20), 40);
    QCOMPARE(snapped.edge, original.edge);
    QVERIFY(qAbs(snapped.fraction - original.fraction) < 0.01);
}

void TestButtonPlacement::clickVsDrag()
{
    QCOMPARE(isClick(QPoint(10, 10), QPoint(13, 12)), true);
    QCOMPARE(isClick(QPoint(10, 10), QPoint(14, 14)), true);
    QCOMPARE(isClick(QPoint(10, 10), QPoint(15, 10)), false);
}

void TestButtonPlacement::persistRoundTrip()
{
    QTemporaryDir dir;
    QSettings settings(dir.filePath("test.ini"), QSettings::IniFormat);

    const ButtonPlacement empty = loadPlacement(settings);
    QCOMPARE(empty.edge, Edge::Right);
    QVERIFY(qFuzzyCompare(empty.fraction, 0.4));

    savePlacement(settings, { Edge::Bottom, 0.25 });
    const ButtonPlacement loaded = loadPlacement(settings);
    QCOMPARE(loaded.edge, Edge::Bottom);
    QVERIFY(qFuzzyCompare(loaded.fraction, 0.25));
}

void TestButtonPlacement::corruptSettingsFallBack()
{
    QTemporaryDir dir;
    QSettings settings(dir.filePath("test.ini"), QSettings::IniFormat);
    settings.setValue("localrdp/button/edge", 42);
    settings.setValue("localrdp/button/fraction", "garbage");

    const ButtonPlacement p = loadPlacement(settings);
    QCOMPARE(p.edge, Edge::Right);
    QVERIFY(p.fraction >= 0.0 && p.fraction <= 1.0);
}
