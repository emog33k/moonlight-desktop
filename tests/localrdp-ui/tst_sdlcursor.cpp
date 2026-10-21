#include "uitests.h"
#include "streaming/localrdp/sdlcursor.h"

#include <QTest>

using namespace LocalRdp;
using namespace LocalRdp::Proto;

static CursorState shape(char fill)
{
    CursorState c;
    c.kind = CursorKind::Shape;
    c.image.width = c.image.height = 16;
    c.image.hotX = 2;
    c.image.hotY = 3;
    c.image.bgra = QByteArray(16 * 16 * 4, fill);
    return c;
}

void TestSdlCursor::showsHidesAndRestores()
{
    SDL_Window* window = UiTest::createStreamWindow(300, 300, 400, 300);
    QVector<int> visibility;
    int toggles = 0;
    {
        SdlCursorHost host([&toggles]() { toggles++; }, [&visibility](int v) { visibility << v; });

        host.showPcCursor(shape('\x40'));
        SDL_Cursor* first = SDL_GetCursor();
        QVERIFY(first != nullptr);
        QVERIFY(first != SDL_GetDefaultCursor());
        QCOMPARE(visibility.last(), (int)SDL_ENABLE);

        host.showPcCursor(shape('\x41'));
        SDL_Cursor* second = SDL_GetCursor();
        QVERIFY(second != first);
        host.showPcCursor(shape('\x40')); // same picture again: same cursor object
        QCOMPARE(SDL_GetCursor(), first);

        CursorState hidden;
        hidden.kind = CursorKind::Hidden;
        host.showPcCursor(hidden);
        QCOMPARE(visibility.last(), (int)SDL_DISABLE);

        CursorState arrow; // Default
        host.showPcCursor(arrow);
        QVERIFY(SDL_GetCursor() != first && SDL_GetCursor() != second);
        QCOMPARE(visibility.last(), (int)SDL_ENABLE);

        host.restoreLocalCursor();
        QCOMPARE(SDL_GetCursor(), SDL_GetDefaultCursor());
        QCOMPARE(visibility.last(), -1);

        host.showPcCursor(shape('\x40'));
        host.toggleSunshineCursor();
        QCOMPARE(toggles, 1);
    }
    // Gone: the normal cursor is back
    QCOMPARE(SDL_GetCursor(), SDL_GetDefaultCursor());
    SDL_DestroyWindow(window);
}

void TestSdlCursor::manyShapesStayBounded()
{
    SDL_Window* window = UiTest::createStreamWindow(300, 300, 400, 300);
    {
        SdlCursorHost host([]() {}, [](int) {});
        for (int i = 0; i < 200; i++) {
            host.showPcCursor(shape((char)i));
            QVERIFY(SDL_GetCursor() != SDL_GetDefaultCursor());
        }
    }
    SDL_DestroyWindow(window);
}
