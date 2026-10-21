#include "uitests.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTest>

#include "streaming/localrdp/streammenucontroller.h"
#include "streaming/localrdp/win32canvas.h"

#include <SDL_syswm.h>
#include <string>
#include <windowsx.h>

#include <cstdio>

namespace UiTest {

SDL_Window* createStreamWindow(int x, int y, int w, int h)
{
    SDL_Window* window = SDL_CreateWindow("LocalRDP UI test", x, y, w, h,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    pump();
    return window;
}

HWND hwndOf(SDL_Window* window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(window, &info);
    return info.info.win.window;
}

QRect clientScreenRect(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    POINT tl = { 0, 0 };
    ClientToScreen(hwnd, &tl);
    return QRect(tl.x, tl.y, rc.right - rc.left, rc.bottom - rc.top);
}

QRect windowScreenRect(HWND hwnd)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    return QRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
}

struct FindData { HWND owner; const wchar_t* cls; HWND found; };

static BOOL CALLBACK findProc(HWND hwnd, LPARAM lp)
{
    auto data = (FindData*)lp;
    wchar_t cls[128];
    GetClassNameW(hwnd, cls, 128);
    if (wcscmp(cls, data->cls) == 0 && GetWindow(hwnd, GW_OWNER) == data->owner) {
        data->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND findOwnedWindow(HWND owner, const wchar_t* className)
{
    FindData data = { owner, className, nullptr };
    EnumThreadWindows(GetCurrentThreadId(), findProc, (LPARAM)&data);
    return data.found;
}

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
        }
        SDL_Delay(5);
    }
}

void saveScreenshot(const QRect& rect, const QString& name)
{
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, rect.width(), rect.height());
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, rect.width(), rect.height(), screen, rect.x(), rect.y(), SRCCOPY | CAPTUREBLT);
    SelectObject(mem, old);

    {
        Gdiplus::Bitmap image(bmp, nullptr);
        // Built-in GDI+ PNG encoder
        const CLSID png = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
        const QString dir = QCoreApplication::applicationDirPath() + "/results";
        QDir().mkpath(dir);
        const std::wstring path = QDir::toNativeSeparators(dir + "/" + name + ".png").toStdWString();
        image.Save(path.c_str(), &png, nullptr);
    }

    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void sendMouse(HWND hwnd, UINT msg, QPoint screenPos)
{
    POINT pt = { screenPos.x(), screenPos.y() };
    ScreenToClient(hwnd, &pt);
    WPARAM keys = (msg == WM_LBUTTONDOWN || msg == WM_MOUSEMOVE) ? MK_LBUTTON : 0;
    SendMessageW(hwnd, msg, keys, MAKELPARAM((short)pt.x, (short)pt.y));
}

}

void deliverMenuEvents(LocalRdp::StreamMenuController& controller, int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_USEREVENT) {
                controller.handleUserEvent(e.user);
            }
        }
        SDL_Delay(5);
    }
}

// Runs one test class, writing its log to <exe dir>/results/<class>.txt and echoing it.
static int runTest(QObject* test)
{
    const QString dir = QCoreApplication::applicationDirPath() + "/results";
    QDir().mkpath(dir);
    const QString file = dir + "/" + test->metaObject()->className() + ".txt";

    QStringList args = { QCoreApplication::applicationFilePath(), "-o", file + ",txt" };
    const int failures = QTest::qExec(test, args);

    QFile log(file);
    if (log.open(QIODevice::ReadOnly)) {
        fputs(log.readAll().constData(), stdout);
        fflush(stdout);
    }
    return failures;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("LocalRDP UI Tests");
    QCoreApplication::setApplicationName("localrdp_ui_tests");

    SDL_SetMainReady();
    // Never take focus from whatever the user is doing (e.g. a full screen game)
    SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int failures = 0;
    {
        TestToolbar t;
        failures += runTest(&t);
    }
    {
        TestStreamMenu t;
        failures += runTest(&t);
    }

    SDL_Quit();
    std::printf("LOCALRDP UI TESTS: %s\n", failures == 0 ? "ALL PASSED" : "FAILED");
    return failures == 0 ? 0 : 1;
}
