#include "tests.h"
#include "streaming/localrdp/windowmemory.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace LocalRdp;

void TestWindowMemory::roundTripPerPc()
{
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QVERIFY(!loadWindowPlacement("pc-a"));
    saveWindowPlacement("pc-a", { QRect(100, 80, 1280, 754), false });
    saveWindowPlacement("pc-b", { QRect(10, 20, 800, 600), true });
    const auto a = loadWindowPlacement("pc-a");
    QVERIFY(a);
    QCOMPARE(a->rect, QRect(100, 80, 1280, 754));
    QCOMPARE(a->maximized, false);
    QCOMPARE(loadWindowPlacement("pc-b")->maximized, true);
}

void TestWindowMemory::fitsDisplays()
{
    const QVector<QRect> screens = { QRect(0, 0, 1920, 1040), QRect(1920, 0, 1920, 1040) };
    QVERIFY(placementFits(QRect(100, 80, 1280, 754), screens));
    QVERIFY(placementFits(QRect(2000, 100, 1280, 754), screens));   // on the second monitor
    QVERIFY(placementFits(QRect(1700, 100, 1280, 754), screens));   // across both
    QVERIFY(!placementFits(QRect(4000, 100, 1280, 754), screens));  // that monitor is gone
    QVERIFY(!placementFits(QRect(100, -500, 1280, 754), screens));  // top (title bar) off screen
    QVERIFY(!placementFits(QRect(100, 80, 100, 50), screens));      // too small to be real
    QVERIFY(!placementFits(QRect(), screens));
}
