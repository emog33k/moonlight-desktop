#include "tests.h"
#include "streaming/localrdp/videoarea.h"

#include <QTest>

using namespace LocalRdp;

static bool same(VideoRect a, int x, int y, int w, int h)
{
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

void TestVideoArea::exactFit()
{
    QVERIFY(same(videoArea(1920, 1080, 1920, 1080, 0), 0, 0, 1920, 1080));
}

void TestVideoArea::pillarbox()
{
    // 16:9 stream in a 4:3 window: bars top and bottom
    QVERIFY(same(videoArea(1600, 1200, 1920, 1080, 0), 0, 150, 1600, 900));
    // 16:9 stream in a 21:9 window: bars left and right
    QVERIFY(same(videoArea(2560, 1080, 1920, 1080, 0), 320, 0, 1920, 1080));
}

void TestVideoArea::toolbarInsetKeepsVideoBelow()
{
    const VideoRect r = videoArea(1920, 1080 + 36, 1920, 1080, 36);
    QVERIFY(same(r, 0, 36, 1920, 1080));
    // Window exactly 16:9: video gets narrower to fit under the toolbar
    const VideoRect s = videoArea(1920, 1080, 1920, 1080, 36);
    QCOMPARE(s.y, 36);
    QCOMPARE(s.h, 1044);
    QCOMPARE(s.w, 1856);
    QCOMPARE(s.x, 32);
}

void TestVideoArea::tinyOrBrokenInputs()
{
    QVERIFY(same(videoArea(100, 20, 1920, 1080, 36), 0, 36, 0, 0)); // inset larger than window
    QVERIFY(same(videoArea(0, 0, 1920, 1080, 0), 0, 0, 0, 0));
    QVERIFY(same(videoArea(800, 600, 0, 0, 0), 0, 0, 800, 600));  // unknown stream size: whole area
}

void TestVideoArea::insetSetting()
{
    setVideoTopInset(40);
    QCOMPARE(videoTopInset(), 40);
    setVideoTopInset(-5);
    QCOMPARE(videoTopInset(), 0);
    setVideoTopInset(0);
}
