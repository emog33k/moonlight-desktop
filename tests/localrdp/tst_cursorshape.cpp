#include "tests.h"
#include "localrdp-shared/cursorshape.h"
#include "cursorwatch_win32.h"

#include <QFile>
#include <cstring>
#include <QtEndian>
#include <QTest>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace LocalRdp;
using namespace LocalRdp::Proto;

static CursorImage sample(int w, int h)
{
    CursorImage c;
    c.width = w;
    c.height = h;
    c.hotX = w / 2;
    c.hotY = 1;
    c.bgra.resize(w * h * 4);
    for (int i = 0; i < c.bgra.size(); i++) {
        c.bgra[i] = (char)(i * 7);
    }
    return c;
}

static QByteArray header(int w, int h, int hx, int hy)
{
    QByteArray b(8, 0);
    qToBigEndian<quint16>((quint16)w, b.data());
    qToBigEndian<quint16>((quint16)h, b.data() + 2);
    qToBigEndian<quint16>((quint16)hx, b.data() + 4);
    qToBigEndian<quint16>((quint16)hy, b.data() + 6);
    return b;
}

void TestCursorShape::roundTrip()
{
    const CursorImage c = sample(32, 48);
    const auto decoded = decodeCursorImage(encodeCursorImage(c));
    QVERIFY(decoded);
    QVERIFY(*decoded == c);
}

void TestCursorShape::badSizesRejected()
{
    QVERIFY(!decodeCursorImage(QByteArray()));
    QVERIFY(!decodeCursorImage(header(0, 10, 0, 0)));
    QVERIFY(!decodeCursorImage(header(kMaxCursorSide + 1, 1, 0, 0) + QByteArray((kMaxCursorSide + 1) * 4, 0)));
    // Pixel data one byte short / one byte long
    QVERIFY(!decodeCursorImage(header(4, 4, 0, 0) + QByteArray(4 * 4 * 4 - 1, 0)));
    QVERIFY(!decodeCursorImage(header(4, 4, 0, 0) + QByteArray(4 * 4 * 4 + 1, 0)));
    QVERIFY(decodeCursorImage(header(4, 4, 0, 0) + QByteArray(4 * 4 * 4, 0)));
}

void TestCursorShape::hotspotOutsideRejected()
{
    QVERIFY(!decodeCursorImage(header(4, 4, 4, 0) + QByteArray(64, 0)));
    QVERIFY(!decodeCursorImage(header(4, 4, 0, 4) + QByteArray(64, 0)));
    QVERIFY(decodeCursorImage(header(4, 4, 3, 3) + QByteArray(64, 0)));
}

static int opaquePixels(const CursorImage& c)
{
    int n = 0;
    for (int i = 3; i < c.bgra.size(); i += 4) {
        n += (uchar)c.bgra[i] == 255;
    }
    return n;
}

// LOCALRDP_DUMP_CURSORS=<dir>: writes <name>.raw (u32 w, u32 h, BGRA) for a human to look at
static void dump(const CursorImage& c, const char* name)
{
    const QString dir = qEnvironmentVariable("LOCALRDP_DUMP_CURSORS");
    if (dir.isEmpty()) {
        return;
    }
    QFile f(dir + "/" + name + ".raw");
    if (f.open(QIODevice::WriteOnly)) {
        const quint32 dims[2] = { (quint32)c.width, (quint32)c.height };
        f.write((const char*)dims, sizeof(dims));
        f.write(c.bgra);
    }
}

void TestCursorShape::capturesSystemArrow()
{
    const CursorImage c = captureCursorImage(LoadCursor(nullptr, IDC_ARROW));
    dump(c, "arrow");
    QVERIFY(c.width >= 16 && c.width <= kMaxCursorSide);
    QCOMPARE(c.bgra.size(), c.width * c.height * 4);
    QVERIFY(opaquePixels(c) > 20);              // the arrow itself
    QVERIFY(opaquePixels(c) < c.width * c.height); // and transparent around it
    QVERIFY(c.hotX < 4 && c.hotY < 4);          // tip at the top left
    QVERIFY(decodeCursorImage(encodeCursorImage(c)));
}

void TestCursorShape::invertingCursorVisible()
{
    // A classic monochrome cursor whose pixels invert the screen (like the
    // default text I-beam): a 2 px vertical bar at x = 15..16, y = 4..27.
    // It must come out black with a light outline, not invisible.
    BYTE andMask[32 * 4], xorMask[32 * 4];
    memset(andMask, 0xFF, sizeof(andMask));
    memset(xorMask, 0x00, sizeof(xorMask));
    for (int y = 4; y < 28; y++) {
        xorMask[y * 4 + 1] = 0x01; // x = 15
        xorMask[y * 4 + 2] = 0x80; // x = 16
    }
    HCURSOR cursor = CreateCursor(GetModuleHandleW(nullptr), 15, 16, 32, 32, andMask, xorMask);
    QVERIFY(cursor != nullptr);
    const CursorImage c = captureCursorImage(cursor);
    DestroyCursor(cursor);
    dump(c, "invert");

    QCOMPARE(c.width, 32);
    QCOMPARE(c.height, 32);
    QCOMPARE(c.hotX, 15);
    auto pixel = [&c](int x, int y) {
        const uchar* p = (const uchar*)c.bgra.constData() + (y * c.width + x) * 4;
        return QVector<int>({ p[0], p[1], p[2], p[3] });
    };
    QCOMPARE(pixel(15, 10), QVector<int>({ 0, 0, 0, 255 }));       // the bar: black
    QCOMPARE(pixel(16, 20), QVector<int>({ 0, 0, 0, 255 }));
    QCOMPARE(pixel(14, 10), QVector<int>({ 255, 255, 255, 255 })); // outline: white
    QCOMPARE(pixel(17, 10), QVector<int>({ 255, 255, 255, 255 }));
    QCOMPARE(pixel(15, 3), QVector<int>({ 255, 255, 255, 255 }));
    QCOMPARE(pixel(5, 5)[3], 0);                                   // elsewhere: transparent
    QCOMPARE(pixel(13, 10)[3], 0);
}

void TestCursorShape::watcherReportsChangesOnly()
{
    CursorWatcher watcher;
    HCURSOR current = LoadCursor(nullptr, IDC_ARROW);
    bool showing = true;
    watcher.setSourceForTests([&](HCURSOR* cursor, bool* visible) {
        *cursor = current;
        *visible = showing;
        return true;
    });

    const CursorState a = watcher.read();
    QCOMPARE(a.kind, CursorKind::Shape);
    const CursorState again = watcher.read();
    QCOMPARE(again.key, a.key);

    current = LoadCursor(nullptr, IDC_HAND);
    const CursorState b = watcher.read();
    QCOMPARE(b.kind, CursorKind::Shape);
    QVERIFY(b.key != a.key);
    QVERIFY(!(b.image == a.image));

    showing = false;
    QCOMPARE(watcher.read().kind, CursorKind::Hidden);

    watcher.setSourceForTests([](HCURSOR*, bool*) { return false; }); // e.g. lock screen
    QCOMPARE(watcher.read().kind, CursorKind::Default);
}

void TestCursorShape::largeCursorScaledDown()
{
    // Accessibility pointers can be bigger than we send: scaled, not dropped
    const int side = 300;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = side;
    bi.bmiHeader.biHeight = -side;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    QVERIFY(color != nullptr);
    quint32* px = (quint32*)bits;
    for (int i = 0; i < side * side; i++) {
        px[i] = 0xFF2060A0; // opaque
    }
    HBITMAP mask = CreateBitmap(side, side, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = FALSE;
    ii.xHotspot = 150;
    ii.yHotspot = 30;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HCURSOR cursor = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    QVERIFY(cursor != nullptr);

    const CursorImage c = captureCursorImage(cursor);
    DestroyCursor(cursor);
    QCOMPARE(c.width, kMaxCursorSide);
    QCOMPARE(c.height, kMaxCursorSide);
    QVERIFY(qAbs(c.hotX - 128) <= 1);
    QVERIFY(qAbs(c.hotY - 26) <= 1);
    QVERIFY(opaquePixels(c) > kMaxCursorSide * kMaxCursorSide / 2);
}
