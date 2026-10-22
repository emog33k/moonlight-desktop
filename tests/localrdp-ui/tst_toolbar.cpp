#include "uitests.h"
#include "streaming/localrdp/menulayout.h"
#include "streaming/localrdp/streammenucontroller.h"
#include "streaming/localrdp/streammenumodel.h"
#include "streaming/localrdp/toolbarlayout.h"
#include "streaming/localrdp/videoarea.h"
#include "streaming/localrdp/win32canvas.h"

#include <QSettings>
#include <QTest>

using namespace LocalRdp;

static const wchar_t* const kToolbarClass = L"LocalRdpToolbar";
static const wchar_t* const kMenuClass = L"LocalRdpStreamMenu";

void TestToolbar::init()
{
    m_SettingsDir = new QTemporaryDir();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_SettingsDir->path());
    m_Window = UiTest::createStreamWindow(220, 160, 1000, 650);
    QVERIFY(m_Window != nullptr);
    m_Owner = UiTest::hwndOf(m_Window);
    m_Host = new FakeHost();
    m_Controller = new StreamMenuController(m_Window, m_Host);
    m_Controller->setBringToFrontOnClick(false);
    UiTest::pump(50);
}

void TestToolbar::cleanup()
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

HWND TestToolbar::toolbarHwnd()
{
    return UiTest::findOwnedWindow(m_Owner, kToolbarClass);
}

HWND TestToolbar::menuHwnd()
{
    return UiTest::findOwnedWindow(m_Owner, kMenuClass);
}

bool TestToolbar::menuShown()
{
    HWND menu = menuHwnd();
    return menu != nullptr && IsWindowVisible(menu);
}

QRect TestToolbar::buttonRect(int index)
{
    return m_Controller->toolbarButtonRect(index);
}

int TestToolbar::buttonIndex(StreamAction action)
{
    const QVector<ToolbarButton> b = toolbarButtons();
    for (int i = 0; i < b.size(); i++) {
        if (b[i].kind == ToolbarButton::Kind::Action && b[i].action == action) {
            return i;
        }
    }
    return -1;
}

int TestToolbar::sectionIndex(MenuSection section)
{
    const QVector<ToolbarButton> b = toolbarButtons();
    for (int i = 0; i < b.size(); i++) {
        if (b[i].kind == ToolbarButton::Kind::Section && b[i].section == section) {
            return i;
        }
    }
    return -1;
}

void TestToolbar::spansTopOfClientArea()
{
    HWND toolbar = toolbarHwnd();
    QVERIFY(toolbar != nullptr);
    QVERIFY(IsWindowVisible(toolbar));
    const QRect client = UiTest::clientScreenRect(m_Owner);
    const int height = (int)(kToolbarBaseHeight * dpiScaleForWindow(m_Owner) + 0.5);
    QCOMPARE(UiTest::windowScreenRect(toolbar), QRect(client.topLeft(), QSize(client.width(), height)));
    // Never takes the keyboard from the stream
    QVERIFY(GetWindowLongW(toolbar, GWL_EXSTYLE) & WS_EX_NOACTIVATE);
}

void TestToolbar::videoInsetFollowsMode()
{
    const int height = UiTest::windowScreenRect(toolbarHwnd()).height();
    QCOMPARE(videoTopInset(), height);
    m_Controller->setFullscreenLayout(true);
    QCOMPARE(videoTopInset(), 0);
    m_Controller->setFullscreenLayout(false);
    QCOMPARE(videoTopInset(), height);
    delete m_Controller;
    m_Controller = nullptr;
    QCOMPARE(videoTopInset(), 0); // nothing left over for the next stream
}

void TestToolbar::followsWindowResize()
{
    SDL_SetWindowSize(m_Window, 760, 500);
    SDL_SetWindowPosition(m_Window, 260, 190);
    UiTest::pump(150);
    const QRect client = UiTest::clientScreenRect(m_Owner);
    const QRect toolbar = UiTest::windowScreenRect(toolbarHwnd());
    QCOMPARE(toolbar.topLeft(), client.topLeft());
    QCOMPARE(toolbar.width(), client.width());
    // The disconnect button stays at the right edge
    QCOMPARE(buttonRect(buttonIndex(StreamAction::Disconnect)).right(), client.right());
}

void TestToolbar::hiddenWhileMinimized()
{
    SDL_WindowEvent e = {};
    e.type = SDL_WINDOWEVENT;
    e.event = SDL_WINDOWEVENT_MINIMIZED;
    m_Controller->handleWindowEvent(e);
    QVERIFY(!IsWindowVisible(toolbarHwnd()));
    e.event = SDL_WINDOWEVENT_RESTORED;
    m_Controller->handleWindowEvent(e);
    QVERIFY(IsWindowVisible(toolbarHwnd()));
}

void TestToolbar::sectionPressOpensItsDropdown()
{
    const int clip = sectionIndex(MenuSection::Clipboard);
    const QRect button = buttonRect(clip);
    QVERIFY(!button.isEmpty());
    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONDOWN, button.center());
    deliverMenuEvents(*m_Controller);
    QVERIFY(m_Controller->isMenuOpen());

    const QRect menu = UiTest::windowScreenRect(menuHwnd());
    QCOMPARE(menu.size(), layoutMenu(buildSectionMenu(MenuSection::Clipboard, m_Host->state), 1.0).size);
    QCOMPARE(menu.left(), button.left());
    QVERIFY(menu.top() > button.bottom() && menu.top() <= button.bottom() + 3);
    QVERIFY(m_Host->performed.isEmpty());
}

void TestToolbar::pressSameSectionAgainCloses()
{
    const QRect button = buttonRect(sectionIndex(MenuSection::Mouse));
    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONDOWN, button.center());
    deliverMenuEvents(*m_Controller);
    QVERIFY(m_Controller->isMenuOpen());

    // The open menu holds the mouse, so the press arrives at the menu window
    UiTest::sendMouse(menuHwnd(), WM_LBUTTONDOWN, button.center());
    UiTest::sendMouse(menuHwnd(), WM_LBUTTONUP, button.center());
    deliverMenuEvents(*m_Controller);
    QVERIFY(!m_Controller->isMenuOpen());
    QVERIFY(!menuShown());
    QVERIFY(m_Host->performed.isEmpty());
}

void TestToolbar::hoverOtherSectionSwitches()
{
    const QRect mouse = buttonRect(sectionIndex(MenuSection::Mouse));
    const QRect screen = buttonRect(sectionIndex(MenuSection::Screen));
    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONDOWN, mouse.center());
    deliverMenuEvents(*m_Controller);
    QCOMPARE(UiTest::windowScreenRect(menuHwnd()).left(), mouse.left());

    UiTest::sendMouse(menuHwnd(), WM_MOUSEMOVE, screen.center());
    deliverMenuEvents(*m_Controller);
    QVERIFY(m_Controller->isMenuOpen());
    const QRect menu = UiTest::windowScreenRect(menuHwnd());
    QCOMPARE(menu.left(), screen.left());
    QCOMPARE(menu.size(), layoutMenu(buildSectionMenu(MenuSection::Screen, m_Host->state), 1.0).size);

    // Switching more than once still closes cleanly with Esc
    UiTest::sendMouse(menuHwnd(), WM_MOUSEMOVE, mouse.center());
    deliverMenuEvents(*m_Controller);
    QCOMPARE(UiTest::windowScreenRect(menuHwnd()).left(), mouse.left());
    SDL_KeyboardEvent esc = {};
    esc.type = SDL_KEYDOWN;
    esc.keysym.scancode = SDL_SCANCODE_ESCAPE;
    QVERIFY(m_Controller->handleKeyEvent(esc));
    QVERIFY(!m_Controller->isMenuOpen());
}

void TestToolbar::actionButtonRunsOnRelease()
{
    const QRect minimize = buttonRect(buttonIndex(StreamAction::Minimize));
    QVERIFY(!minimize.isEmpty());
    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONDOWN, minimize.center());
    deliverMenuEvents(*m_Controller, 50);
    QVERIFY(m_Host->performed.isEmpty()); // not on press
    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONUP, minimize.center());
    deliverMenuEvents(*m_Controller);
    QCOMPARE(m_Host->performed, QVector<StreamAction>({ StreamAction::Minimize }));
}

void TestToolbar::actionReleasedElsewhereDoesNothing()
{
    const QRect disconnect = buttonRect(buttonIndex(StreamAction::Disconnect));
    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONDOWN, disconnect.center());
    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONUP, disconnect.center() + QPoint(0, 200));
    deliverMenuEvents(*m_Controller);
    QVERIFY(m_Host->performed.isEmpty());
}

void TestToolbar::actionButtonWhileMenuOpenOnlyCloses()
{
    // A press on Disconnect while a dropdown is open closes the dropdown; it
    // does not disconnect (that needs its own click)
    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONDOWN, buttonRect(sectionIndex(MenuSection::Pc)).center());
    deliverMenuEvents(*m_Controller);
    QVERIFY(m_Controller->isMenuOpen());

    const QPoint disconnect = buttonRect(buttonIndex(StreamAction::Disconnect)).center();
    UiTest::sendMouse(menuHwnd(), WM_LBUTTONDOWN, disconnect);
    UiTest::sendMouse(menuHwnd(), WM_LBUTTONUP, disconnect);
    deliverMenuEvents(*m_Controller);
    QVERIFY(m_Host->performed.isEmpty());
    QVERIFY(!m_Controller->isMenuOpen());

    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONDOWN, disconnect);
    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONUP, disconnect);
    deliverMenuEvents(*m_Controller);
    QCOMPARE(m_Host->performed, QVector<StreamAction>({ StreamAction::Disconnect }));
}

void TestToolbar::hotkeyMenuOpensUnderToolbar()
{
    m_Controller->toggleMenu();
    QVERIFY(m_Controller->isMenuOpen());
    const QRect toolbar = UiTest::windowScreenRect(toolbarHwnd());
    const QRect menu = UiTest::windowScreenRect(menuHwnd());
    QCOMPARE(menu.size(), layoutMenu(buildToolbarMenu(MenuSection::Mouse, m_Host->state), 1.0).size);
    QVERIFY(menu.top() > toolbar.bottom());
    m_Controller->toggleMenu();
    QVERIFY(!m_Controller->isMenuOpen());
}

void TestToolbar::autoHidesInFullscreen()
{
    const QRect client = UiTest::clientScreenRect(m_Owner);
    QPoint cursor = client.center();
    m_Controller->setCursorProviderForTests([&]() { return cursor; });

    m_Controller->setFullscreenLayout(true);
    HWND toolbar = toolbarHwnd();
    QVERIFY(IsWindowVisible(toolbar)); // shown for a moment so people know it is there
    // Compact and centred: the screen's top corners stay free
    const QRect bar = UiTest::windowScreenRect(toolbar);
    QVERIFY(bar.width() < client.width());
    QVERIFY(qAbs(bar.center().x() - client.center().x()) <= 1);
    UiTest::pump(1900);
    QVERIFY(!IsWindowVisible(toolbar));

    cursor = QPoint(client.right(), client.top()); // top-right corner (closing a PC window)
    UiTest::pump(800);
    QVERIFY(!IsWindowVisible(toolbar));

    cursor = QPoint(client.center().x(), client.top()); // top edge above the bar...
    UiTest::pump(120);
    QVERIFY(!IsWindowVisible(toolbar));                 // ...a flick past does nothing
    UiTest::pump(500);
    QVERIFY(IsWindowVisible(toolbar));                  // resting there brings it out

    cursor = QPoint(client.center().x(), client.top() + 10); // on the toolbar: stays
    UiTest::pump(1200);
    QVERIFY(IsWindowVisible(toolbar));

    cursor = client.center();
    UiTest::pump(1200);
    QVERIFY(!IsWindowVisible(toolbar));

    m_Controller->toggleMenu(); // menu open: toolbar out and stays
    QVERIFY(IsWindowVisible(toolbar));
    UiTest::pump(1200);
    QVERIFY(IsWindowVisible(toolbar));
    m_Controller->toggleMenu();

    m_Controller->setFullscreenLayout(false);
    UiTest::pump(200);
    QVERIFY(IsWindowVisible(toolbar));
}

void TestToolbar::toastBelowToolbar()
{
    m_Controller->showToast(QStringLiteral("Файлы в буфере ПК — нажмите Ctrl+V"), 400);
    HWND toast = UiTest::findOwnedWindow(m_Owner, L"LocalRdpToast");
    QVERIFY(toast != nullptr);
    QVERIFY(IsWindowVisible(toast));
    const QRect t = UiTest::windowScreenRect(toast);
    const QRect bar = UiTest::windowScreenRect(toolbarHwnd());
    QVERIFY(t.top() > bar.bottom());
    QVERIFY(qAbs(t.center().x() - bar.center().x()) <= 2);
    UiTest::pump(800);
    QVERIFY(!IsWindowVisible(toast));
}

void TestToolbar::destroyRemovesToolbar()
{
    m_Controller->toggleMenu();
    delete m_Controller;
    m_Controller = nullptr;
    QVERIFY(toolbarHwnd() == nullptr);
    QVERIFY(menuHwnd() == nullptr);
}

void TestToolbar::screenshots()
{
    // For a human to look at (results/*.png); asserts nothing beyond not crashing.
    m_Host->state.agentStatus = AgentStatus::Connected;
    UiTest::pump(300); // status dot refresh
    const QRect bar = UiTest::windowScreenRect(toolbarHwnd());
    UiTest::sendMouse(toolbarHwnd(), WM_MOUSEMOVE, buttonRect(buttonIndex(StreamAction::Disconnect)).center());
    UiTest::pump(100);
    UiTest::saveScreenshot(bar, "toolbar");

    UiTest::sendMouse(toolbarHwnd(), WM_LBUTTONDOWN, buttonRect(sectionIndex(MenuSection::Screen)).center());
    deliverMenuEvents(*m_Controller);
    UiTest::pump(100);
    UiTest::saveScreenshot(bar.united(UiTest::windowScreenRect(menuHwnd())).adjusted(0, 0, 0, 10), "toolbar-screen-menu");
    m_Controller->toggleMenu();
}
