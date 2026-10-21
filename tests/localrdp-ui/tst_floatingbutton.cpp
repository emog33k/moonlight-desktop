#include "uitests.h"
#include "streaming/localrdp/buttonplacement.h"
#include "streaming/localrdp/floatingbutton_win32.h"
#include "streaming/localrdp/streammenucontroller.h"
#include "streaming/localrdp/win32canvas.h"

#include <QElapsedTimer>
#include <QSettings>
#include <QTest>

#include <memory>

using namespace LocalRdp;

static const wchar_t* const kButtonClass = L"LocalRdpFloatingButton";
static const int kSize = 40;   // test process is DPI-unaware, so 96 DPI sizes
static const int kMargin = 8;

void TestFloatingButton::init()
{
    m_SettingsDir = new QTemporaryDir();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_SettingsDir->path());
    m_Window = UiTest::createStreamWindow(200, 200, 1000, 600);
    QVERIFY(m_Window != nullptr);
}

void TestFloatingButton::cleanup()
{
    SDL_DestroyWindow(m_Window);
    m_Window = nullptr;
    UiTest::pump(50);
    delete m_SettingsDir;
    m_SettingsDir = nullptr;
}

void TestFloatingButton::defaultSpotRightEdge()
{
    GdiplusSession gdiplus;
    HWND owner = UiTest::hwndOf(m_Window);
    FloatingButtonWindow button(owner, []() {});
    UiTest::pump();

    const QPoint expected = placeButton(UiTest::clientScreenRect(owner), kSize, kMargin, ButtonPlacement());
    QCOMPARE(button.screenRect(), QRect(expected, QSize(kSize, kSize)));
    HWND hwnd = UiTest::findOwnedWindow(owner, kButtonClass);
    QVERIFY(hwnd != nullptr);
    QVERIFY(IsWindowVisible(hwnd));
    QCOMPARE(UiTest::windowScreenRect(hwnd), button.screenRect());

    // For a human check of the look: idle (half transparent) button on the window edge
    UiTest::saveScreenshot(button.screenRect().adjusted(-60, -40, 20, 40), "button-idle");
}

void TestFloatingButton::followsWindowMoveAndResize()
{
    FakeHost host;
    StreamMenuController controller(m_Window, &host);
    HWND owner = UiTest::hwndOf(m_Window);
    HWND hwnd = UiTest::findOwnedWindow(owner, kButtonClass);
    QVERIFY(hwnd != nullptr);

    SDL_SetWindowPosition(m_Window, 320, 260);
    UiTest::pump();
    QCOMPARE(UiTest::windowScreenRect(hwnd).topLeft(),
             placeButton(UiTest::clientScreenRect(owner), kSize, kMargin, ButtonPlacement()));

    SDL_SetWindowSize(m_Window, 640, 400);
    UiTest::pump();
    QCOMPARE(UiTest::windowScreenRect(hwnd).topLeft(),
             placeButton(UiTest::clientScreenRect(owner), kSize, kMargin, ButtonPlacement()));
}

void TestFloatingButton::hiddenWhileMinimized()
{
    FakeHost host;
    StreamMenuController controller(m_Window, &host);
    HWND owner = UiTest::hwndOf(m_Window);
    HWND hwnd = UiTest::findOwnedWindow(owner, kButtonClass);
    QVERIFY(hwnd != nullptr);

    // Feed the controller the same window events the session loop would
    auto deliver = [&]() {
        SDL_Event e;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 300) {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_WINDOWEVENT) {
                    controller.handleWindowEvent(e.window);
                }
            }
            SDL_Delay(5);
        }
    };

    SDL_MinimizeWindow(m_Window);
    deliver();
    QVERIFY(!IsWindowVisible(hwnd));

    SDL_RestoreWindow(m_Window);
    deliver();
    QVERIFY(IsWindowVisible(hwnd));
    QCOMPARE(UiTest::windowScreenRect(hwnd).topLeft(),
             placeButton(UiTest::clientScreenRect(owner), kSize, kMargin, ButtonPlacement()));
}

void TestFloatingButton::smallMoveIsClick()
{
    GdiplusSession gdiplus;
    HWND owner = UiTest::hwndOf(m_Window);
    int clicks = 0;
    FloatingButtonWindow button(owner, [&]() { clicks++; });
    HWND hwnd = UiTest::findOwnedWindow(owner, kButtonClass);
    const QRect before = button.screenRect();
    const QPoint p = before.center();

    UiTest::sendMouse(hwnd, WM_LBUTTONDOWN, p);
    UiTest::sendMouse(hwnd, WM_MOUSEMOVE, p + QPoint(3, 2));
    UiTest::sendMouse(hwnd, WM_LBUTTONUP, p + QPoint(3, 2));

    QCOMPARE(clicks, 1);
    QCOMPARE(button.screenRect(), before);
}

void TestFloatingButton::dragSnapsToEdgeAndPersists()
{
    GdiplusSession gdiplus;
    HWND owner = UiTest::hwndOf(m_Window);
    const QRect client = UiTest::clientScreenRect(owner);
    {
        FloatingButtonWindow button(owner, []() {});
        HWND hwnd = UiTest::findOwnedWindow(owner, kButtonClass);
        const QPoint start = button.screenRect().center();
        // Drag to just right of the bottom-left corner, closest to the bottom edge
        const QPoint target(client.left() + 300, client.bottom() - 10);

        UiTest::sendMouse(hwnd, WM_LBUTTONDOWN, start);
        for (int i = 1; i <= 10; i++) {
            UiTest::sendMouse(hwnd, WM_MOUSEMOVE, start + (target - start) * i / 10);
        }
        UiTest::sendMouse(hwnd, WM_LBUTTONUP, target);

        QSettings settings;
        const ButtonPlacement saved = loadPlacement(settings);
        QCOMPARE(saved.edge, Edge::Bottom);
        QCOMPARE(button.screenRect().topLeft(), placeButton(client, kSize, kMargin, saved));
    }

    // A new button (next session) comes back at the saved spot
    FloatingButtonWindow again(owner, []() {});
    QSettings settings;
    QCOMPARE(again.screenRect().topLeft(), placeButton(client, kSize, kMargin, loadPlacement(settings)));
}

void TestFloatingButton::dragDoesNotClick()
{
    GdiplusSession gdiplus;
    HWND owner = UiTest::hwndOf(m_Window);
    int clicks = 0;
    FloatingButtonWindow button(owner, [&]() { clicks++; });
    HWND hwnd = UiTest::findOwnedWindow(owner, kButtonClass);
    const QPoint start = button.screenRect().center();

    UiTest::sendMouse(hwnd, WM_LBUTTONDOWN, start);
    UiTest::sendMouse(hwnd, WM_MOUSEMOVE, start + QPoint(-60, 0));
    UiTest::sendMouse(hwnd, WM_LBUTTONUP, start + QPoint(-60, 0));

    QCOMPARE(clicks, 0);
}

void TestFloatingButton::controllerDestroyRemovesButton()
{
    HWND owner = UiTest::hwndOf(m_Window);
    HWND hwnd = nullptr;
    {
        FakeHost host;
        StreamMenuController controller(m_Window, &host);
        hwnd = UiTest::findOwnedWindow(owner, kButtonClass);
        QVERIFY(hwnd != nullptr);
    }
    QVERIFY(!IsWindow(hwnd));
    QVERIFY(UiTest::findOwnedWindow(owner, kButtonClass) == nullptr);
}
