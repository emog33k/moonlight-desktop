#include "uitests.h"
#include "streaming/localrdp/menulayout.h"
#include "streaming/localrdp/streammenucontroller.h"
#include "streaming/localrdp/streammenumodel.h"
#include "streaming/localrdp/toolbarlayout.h"

#include <QSettings>
#include <QTest>

using namespace LocalRdp;

static const wchar_t* const kToolbarClass = L"LocalRdpToolbar";
static const wchar_t* const kMenuClass = L"LocalRdpStreamMenu";

static SDL_KeyboardEvent keyDown(SDL_Scancode scancode, SDL_Keycode sym, Uint16 mod = KMOD_NONE)
{
    SDL_KeyboardEvent e = {};
    e.type = SDL_KEYDOWN;
    e.state = SDL_PRESSED;
    e.keysym.scancode = scancode;
    e.keysym.sym = sym;
    e.keysym.mod = mod;
    return e;
}

void TestStreamMenu::init()
{
    m_SettingsDir = new QTemporaryDir();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_SettingsDir->path());
    m_Window = UiTest::createStreamWindow(200, 150, 1000, 650);
    QVERIFY(m_Window != nullptr);
    m_Owner = UiTest::hwndOf(m_Window);
    m_Host = new FakeHost();
    m_Controller = new StreamMenuController(m_Window, m_Host);
    m_Controller->setBringToFrontOnClick(qEnvironmentVariableIsSet("LOCALRDP_UI_FOREGROUND"));
    m_Focused = true;
    m_Controller->setFocusProviderForTests([this]() { return m_Focused; });
    m_OpenSection = MenuSection::Mouse;
}

void TestStreamMenu::cleanup()
{
    delete m_Controller;
    m_Controller = nullptr;
    delete m_Host;
    m_Host = nullptr;
    SDL_DestroyWindow(m_Window);
    m_Window = nullptr;
    UiTest::pump(50);
    delete m_SettingsDir;
    m_SettingsDir = nullptr;
}

HWND TestStreamMenu::menuHwnd()
{
    return UiTest::findOwnedWindow(m_Owner, kMenuClass);
}

bool TestStreamMenu::menuShown()
{
    HWND menu = menuHwnd();
    return menu != nullptr && IsWindowVisible(menu);
}

void TestStreamMenu::openMenuWith(StreamAction action)
{
    const QVector<ToolbarButton> buttons = toolbarButtons();
    for (int i = 0; i < buttons.size(); i++) {
        if (buttons[i].kind != ToolbarButton::Kind::Section) {
            continue;
        }
        for (const MenuItem& item : buildToolbarMenu(buttons[i].section, m_Host->state)) {
            if (item.action && *item.action == action) {
                HWND toolbar = UiTest::findOwnedWindow(m_Owner, kToolbarClass);
                UiTest::sendMouse(toolbar, WM_LBUTTONDOWN, m_Controller->toolbarButtonRect(i).center());
                deliverMenuEvents(*m_Controller);
                m_OpenSection = buttons[i].section;
                return;
            }
        }
    }
    QFAIL("no toolbar dropdown holds this action");
}

QPoint TestStreamMenu::itemCenterOnScreen(StreamAction action)
{
    const QVector<MenuItem> items = buildToolbarMenu(m_OpenSection, m_Host->state);
    const MenuLayout layout = layoutMenu(items, 1.0);
    const QPoint origin = UiTest::windowScreenRect(menuHwnd()).topLeft();
    for (int i = 0; i < items.size(); i++) {
        if (items[i].action && *items[i].action == action) {
            return origin + layout.itemRects[i].center();
        }
    }
    return QPoint();
}

void TestStreamMenu::click(QPoint screenPos)
{
    UiTest::sendMouse(menuHwnd(), WM_MOUSEMOVE, screenPos);
    UiTest::sendMouse(menuHwnd(), WM_LBUTTONDOWN, screenPos);
    UiTest::sendMouse(menuHwnd(), WM_LBUTTONUP, screenPos);
}

void TestStreamMenu::opensUnderToolbar()
{
    QVERIFY(!m_Controller->isMenuOpen());
    m_Controller->toggleMenu();
    QVERIFY(m_Controller->isMenuOpen());
    QVERIFY(menuShown());

    // The hotkey opens the first dropdown (Мышь), right under its button
    const QRect toolbar = UiTest::windowScreenRect(UiTest::findOwnedWindow(m_Owner, kToolbarClass));
    const QRect menu = UiTest::windowScreenRect(menuHwnd());
    QCOMPARE(menu.size(), layoutMenu(buildToolbarMenu(MenuSection::Mouse, m_Host->state), 1.0).size);
    QVERIFY(menu.top() > toolbar.bottom());
    QVERIFY(menu.top() <= toolbar.bottom() + 3);
    QCOMPARE(menu.left(), m_Controller->toolbarButtonRect(0).left());

    UiTest::pump(100);
    UiTest::saveScreenshot(menu.united(toolbar).adjusted(-10, -10, 10, 10), "menu-open");
}

void TestStreamMenu::toggleTwiceCloses()
{
    m_Controller->toggleMenu();
    m_Controller->toggleMenu();
    QVERIFY(!m_Controller->isMenuOpen());
    QVERIFY(!menuShown());
}

void TestStreamMenu::clickItemPerformsActionAndCloses()
{
    openMenuWith(StreamAction::ToggleStats);
    click(itemCenterOnScreen(StreamAction::ToggleStats));
    deliverMenuEvents(*m_Controller);
    QCOMPARE(m_Host->performed, QVector<StreamAction>({ StreamAction::ToggleStats }));
    QVERIFY(!m_Controller->isMenuOpen());
}

void TestStreamMenu::clickHeaderKeepsOpen()
{
    m_Controller->toggleMenu();
    const MenuLayout layout = layoutMenu(buildToolbarMenu(MenuSection::Mouse, m_Host->state), 1.0);
    const QPoint header = UiTest::windowScreenRect(menuHwnd()).topLeft() + layout.itemRects[0].center();
    click(header);
    QVERIFY(m_Host->performed.isEmpty());
    QVERIFY(m_Controller->isMenuOpen());
}

void TestStreamMenu::clickOutsideClosesWithoutAction()
{
    m_Controller->toggleMenu();
    const QRect menu = UiTest::windowScreenRect(menuHwnd());
    click(menu.topLeft() - QPoint(150, 0));
    QVERIFY(m_Host->performed.isEmpty());
    QVERIFY(!m_Controller->isMenuOpen());
}

void TestStreamMenu::escapeClosesAndIsConsumed()
{
    QVERIFY(!m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE))); // closed: goes to PC
    m_Controller->toggleMenu();
    QVERIFY(m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE)));
    QVERIFY(!m_Controller->isMenuOpen());
    QVERIFY(m_Host->performed.isEmpty());
}

void TestStreamMenu::arrowsAndEnterActivate()
{
    m_Controller->toggleMenu();
    QVERIFY(m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_DOWN, SDLK_DOWN)));
    QVERIFY(m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_DOWN, SDLK_DOWN)));
    QVERIFY(m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_RETURN, SDLK_RETURN)));
    deliverMenuEvents(*m_Controller);
    // 1st actionable: desktop mode, 2nd: game mode
    QCOMPARE(m_Host->performed, QVector<StreamAction>({ StreamAction::SetGameMouseMode }));
    QVERIFY(!m_Controller->isMenuOpen());
}

void TestStreamMenu::menuHotkeyPassesThroughWhenOpen()
{
    m_Controller->toggleMenu();
    const Uint16 mods = KMOD_LCTRL | KMOD_LALT | KMOD_LSHIFT;
    // Not consumed, so the input handler sees Ctrl+Alt+Shift+Space and toggles the menu
    QVERIFY(!m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_SPACE, SDLK_SPACE, mods)));
}

void TestStreamMenu::otherKeysConsumedWhileOpen()
{
    m_Controller->toggleMenu();
    QVERIFY(m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_A, SDLK_a)));
    QVERIFY(m_Controller->isMenuOpen());
}

void TestStreamMenu::gameModeReleasesAndRestoresCapture()
{
    m_Host->state.absoluteMouse = false;
    m_Host->captureActive = true;
    m_Controller->toggleMenu();
    QCOMPARE(m_Host->captureChanges, QVector<bool>({ false }));
    m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE));
    QCOMPARE(m_Host->captureChanges, QVector<bool>({ false, true }));
}

void TestStreamMenu::disconnectDoesNotRecapture()
{
    m_Host->state.absoluteMouse = false;
    m_Host->captureActive = true;
    openMenuWith(StreamAction::Disconnect);
    click(itemCenterOnScreen(StreamAction::Disconnect));
    deliverMenuEvents(*m_Controller);
    QCOMPARE(m_Host->performed, QVector<StreamAction>({ StreamAction::Disconnect }));
    QCOMPARE(m_Host->captureChanges, QVector<bool>({ false }));
}

void TestStreamMenu::desktopModeKeepsCapture()
{
    m_Host->state.absoluteMouse = true;
    m_Host->captureActive = true;
    m_Controller->toggleMenu();
    m_Controller->toggleMenu();
    QVERIFY(m_Host->captureChanges.isEmpty());
}

void TestStreamMenu::minimizeClosesMenu()
{
    m_Controller->toggleMenu();
    SDL_WindowEvent e = {};
    e.type = SDL_WINDOWEVENT;
    e.event = SDL_WINDOWEVENT_MINIMIZED;
    m_Controller->handleWindowEvent(e);
    QVERIFY(!m_Controller->isMenuOpen());
    QVERIFY(!menuShown());
}

void TestStreamMenu::destroyWhileOpenRemovesEverything()
{
    m_Controller->toggleMenu();
    QVERIFY(menuHwnd() != nullptr);
    delete m_Controller;
    m_Controller = nullptr;
    QVERIFY(UiTest::findOwnedWindow(m_Owner, kMenuClass) == nullptr);
    QVERIFY(UiTest::findOwnedWindow(m_Owner, kToolbarClass) == nullptr);
}

void TestStreamMenu::keyUpsPassThroughWhileOpen()
{
    // Releases always reach the PC, so nothing pressed before/around the menu can stick there
    m_Controller->toggleMenu();
    SDL_KeyboardEvent up = keyDown(SDL_SCANCODE_LSHIFT, SDLK_LSHIFT);
    up.type = SDL_KEYUP;
    up.state = SDL_RELEASED;
    QVERIFY(!m_Controller->handleKeyEvent(up));
    QVERIFY(m_Controller->isMenuOpen());
}

void TestStreamMenu::repeatedSpaceDoesNotActivate()
{
    m_Controller->toggleMenu();
    m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_DOWN, SDLK_DOWN));
    SDL_KeyboardEvent repeat = keyDown(SDL_SCANCODE_SPACE, SDLK_SPACE);
    repeat.repeat = 1; // Space still held after Ctrl+Alt+Shift+Space opened the menu
    QVERIFY(m_Controller->handleKeyEvent(repeat));
    deliverMenuEvents(*m_Controller);
    QVERIFY(m_Host->performed.isEmpty());
    QVERIFY(m_Controller->isMenuOpen());
}

void TestStreamMenu::actionRunsFromEventLoopNotWndProc()
{
    openMenuWith(StreamAction::ToggleFullscreen);
    click(itemCenterOnScreen(StreamAction::ToggleFullscreen));
    // Not run inside the window procedure (that is inside SDL's event pump)...
    QVERIFY(m_Host->performed.isEmpty());
    // ...but when the session loop hands the posted event back
    deliverMenuEvents(*m_Controller);
    QCOMPARE(m_Host->performed, QVector<StreamAction>({ StreamAction::ToggleFullscreen }));
}

void TestStreamMenu::pointerLockSuspendedWhileOpen()
{
    m_Host->state.pointerLocked = true;
    m_Controller->toggleMenu();
    QCOMPARE(m_Host->pointerLockSuspends, QVector<bool>({ true }));
    m_Controller->toggleMenu();
    QCOMPARE(m_Host->pointerLockSuspends, QVector<bool>({ true, false }));
}

void TestStreamMenu::openingFromBackgroundActivatesStream()
{
    // This test changes the foreground window, which would pull the user out of
    // whatever they are doing; run it only on request.
    if (!qEnvironmentVariableIsSet("LOCALRDP_UI_FOREGROUND")) {
        QSKIP("Set LOCALRDP_UI_FOREGROUND=1 to run (it switches the active window)");
    }

    // Another window of ours is in front; pressing a toolbar section must bring the stream
    // forward so the menu's mouse capture and keyboard work as expected.
    SDL_Window* other = UiTest::createStreamWindow(50, 50, 300, 200);
    HWND otherHwnd = UiTest::hwndOf(other);
    SetForegroundWindow(otherHwnd);
    UiTest::pump(100);
    if (GetForegroundWindow() != otherHwnd) {
        // Windows only lets a process take the foreground after real user input.
        // With synthetic messages that holds only when the test itself is in front.
        SDL_DestroyWindow(other);
        QSKIP("Test process cannot own the foreground here (another app is active); "
              "covered by the real-stream check instead");
    }

    HWND toolbar = UiTest::findOwnedWindow(m_Owner, kToolbarClass);
    const QPoint c = m_Controller->toolbarButtonRect(0).center();
    UiTest::sendMouse(toolbar, WM_LBUTTONDOWN, c);
    deliverMenuEvents(*m_Controller);

    QVERIFY(m_Controller->isMenuOpen());
    QCOMPARE(GetForegroundWindow(), m_Owner);
    m_Controller->toggleMenu();
    SDL_DestroyWindow(other);
}

void TestStreamMenu::toastShowsThenHides()
{
    m_Controller->showToast(QStringLiteral("Файлы в буфере ПК — нажмите Ctrl+V"), 400);
    HWND toast = UiTest::findOwnedWindow(m_Owner, L"LocalRdpToast");
    QVERIFY(toast != nullptr);
    QVERIFY(IsWindowVisible(toast));
    UiTest::pump(100);
    UiTest::saveScreenshot(UiTest::windowScreenRect(toast).adjusted(-10, -10, 10, 10), "toast");
    UiTest::pump(700);
    QVERIFY(!IsWindowVisible(toast));
}

void TestStreamMenu::displayItemCarriesIndex()
{
    openMenuWith(StreamAction::SwitchDisplay);
    const QVector<MenuItem> items = buildToolbarMenu(MenuSection::Screen, m_Host->state);
    const MenuLayout layout = layoutMenu(items, 1.0);
    const QPoint origin = UiTest::windowScreenRect(menuHwnd()).topLeft();
    int third = -1, seen = 0;
    for (int i = 0; i < items.size(); i++) {
        if (items[i].action && *items[i].action == StreamAction::SwitchDisplay && seen++ == 2) third = i;
    }
    QVERIFY(third >= 0);
    click(origin + layout.itemRects[third].center());
    deliverMenuEvents(*m_Controller);
    QCOMPARE(m_Host->performed, QVector<StreamAction>({ StreamAction::SwitchDisplay }));
    QCOMPARE(m_Host->params, QVector<int>({ 2 }));
}

void TestStreamMenu::leftRightMoveAlongToolbar()
{
    m_Controller->toggleMenu(); // Мышь
    QCOMPARE(UiTest::windowScreenRect(menuHwnd()).left(), m_Controller->toolbarButtonRect(0).left());
    QVERIFY(m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_RIGHT, SDLK_RIGHT)));
    QCOMPARE(UiTest::windowScreenRect(menuHwnd()).left(), m_Controller->toolbarButtonRect(1).left());
    QVERIFY(m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_LEFT, SDLK_LEFT)));
    QVERIFY(m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_LEFT, SDLK_LEFT))); // wraps to the last section (ПК)
    const QRect menu = UiTest::windowScreenRect(menuHwnd());
    QCOMPARE(menu.size(), layoutMenu(buildToolbarMenu(MenuSection::Pc, m_Host->state), 1.0).size);
    QVERIFY(m_Controller->isMenuOpen());
    QVERIFY(m_Host->performed.isEmpty());
}

void TestStreamMenu::captureNotRestoredWithoutFocus()
{
    // Game mode, menu open, then the user Alt+Tabs away: the menu closes but the
    // mouse is not grabbed again by a window that is not in front
    m_Host->state.absoluteMouse = false;
    m_Host->captureActive = true;
    m_Controller->toggleMenu();
    QCOMPARE(m_Host->captureChanges, QVector<bool>({ false }));
    m_Focused = false;
    m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE));
    QCOMPARE(m_Host->captureChanges, QVector<bool>({ false }));
}

void TestStreamMenu::volumeSlider()
{
    m_Host->state.streamVolume = 70;
    openMenuWith(StreamAction::SetVolume);
    const QVector<MenuItem> items = buildToolbarMenu(MenuSection::Sound, m_Host->state);
    const MenuLayout layout = layoutMenu(items, 1.0);
    int row = -1;
    for (int i = 0; i < items.size(); i++) {
        if (items[i].kind == MenuItemKind::Slider) row = i;
    }
    QVERIFY(row >= 0);
    const QPoint origin = UiTest::windowScreenRect(menuHwnd()).topLeft();
    const QRect rect = layout.itemRects[row];
    const QPair<int, int> track = sliderTrack(rect, 1.0);
    const int y = origin.y() + rect.center().y();
    auto at = [&](int percent) { return QPoint(origin.x() + track.first + (track.second - track.first) * percent / 100, y); };

    // Click at a quarter: 25 %
    UiTest::sendMouse(menuHwnd(), WM_MOUSEMOVE, at(25));
    UiTest::sendMouse(menuHwnd(), WM_LBUTTONDOWN, at(25));
    deliverMenuEvents(*m_Controller, 50);
    QVERIFY(m_Host->performed.contains(StreamAction::SetVolume));
    QVERIFY(qAbs(m_Host->params.last() - 25) <= 1);
    // Drag to three quarters
    UiTest::sendMouse(menuHwnd(), WM_MOUSEMOVE, at(75));
    UiTest::sendMouse(menuHwnd(), WM_LBUTTONUP, at(75));
    deliverMenuEvents(*m_Controller);
    QVERIFY(qAbs(m_Host->params.last() - 75) <= 1);
    QVERIFY(m_Controller->isMenuOpen()); // stays open
    // Arrow keys move it in 5 % steps while it is highlighted
    const int before = m_Host->params.last();
    QVERIFY(m_Controller->handleKeyEvent(keyDown(SDL_SCANCODE_RIGHT, SDLK_RIGHT)));
    deliverMenuEvents(*m_Controller);
    QCOMPARE(m_Host->params.last(), qMin(100, before + 5));
    QVERIFY(m_Controller->isMenuOpen());
    QCOMPARE(UiTest::windowScreenRect(menuHwnd()).topLeft(), origin); // still the Sound menu
}
