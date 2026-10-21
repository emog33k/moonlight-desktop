#include "streammenucontroller.h"
#include "floatingbutton_win32.h"
#include "streammenu_win32.h"
#include "streammenumodel.h"
#include "toast_win32.h"
#include "win32canvas.h"

#include <SDL_syswm.h>

namespace LocalRdp {

static HWND hwndForWindow(SDL_Window* window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) {
        return nullptr;
    }
    return info.info.win.window;
}

static void postUserEvent(int code, int data = 0)
{
    SDL_Event event = {};
    event.type = SDL_USEREVENT;
    event.user.code = code;
    event.user.data1 = (void*)(intptr_t)data;
    SDL_PushEvent(&event);
}

StreamMenuController::StreamMenuController(SDL_Window* window, StreamMenuHost* host)
    : m_Window(window),
      m_Host(host),
      m_Gdiplus(new GdiplusSession())
{
    m_Hwnd = hwndForWindow(window);
    if (m_Hwnd == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "LocalRDP: no native window handle, in-stream menu disabled");
        return;
    }

    // Window procedures run inside SDL's event pump, so they only post events;
    // the session loop calls back into handleUserEvent().
    m_Button.reset(new FloatingButtonWindow(m_Hwnd, [this]() {
        // Bring the stream forward so the menu's mouse capture and the keyboard
        // (Esc, arrows) work even if another app was active. We just received
        // the click, so Windows allows this.
        if (m_BringToFrontOnClick && GetForegroundWindow() != m_Hwnd) {
            SetForegroundWindow(m_Hwnd);
        }
        postUserEvent(SDL_CODE_TOGGLE_STREAM_MENU);
    }));
    m_Menu.reset(new StreamMenuWindow(m_Hwnd,
                                      [](StreamAction action) { postUserEvent(SDL_CODE_STREAM_MENU_ACTION, (int)action); },
                                      []() { postUserEvent(SDL_CODE_STREAM_MENU_DISMISSED); }));
    m_Toast.reset(new ToastWindow(m_Hwnd));
}

StreamMenuController::~StreamMenuController()
{
    // Windows must go before GDI+ shuts down. The stream is ending, so the
    // capture we may have released is not given back.
    m_Toast.reset();
    m_Menu.reset();
    m_Button.reset();
    m_Gdiplus.reset();
}

bool StreamMenuController::isMenuOpen() const
{
    return m_Menu && m_Menu->isOpen();
}

void StreamMenuController::toggleMenu()
{
    if (isMenuOpen()) {
        closeMenu(true);
    }
    else {
        openMenu();
    }
}

void StreamMenuController::openMenu()
{
    if (!m_Button || !m_Menu) {
        return;
    }
    const StreamState state = m_Host->queryState();

    // In game mode the cursor is hidden and locked; free it while the menu is up.
    if (!state.absoluteMouse && m_Host->isCaptureActive()) {
        m_Host->setCaptureActive(false);
        m_CaptureReleased = true;
    }
    // A cursor clipped to the video would not reach every menu item.
    if (state.pointerLocked) {
        m_Host->suspendPointerLock(true);
        m_PointerLockSuspended = true;
    }

    m_Button->setKeepVisible(true);
    m_Menu->show(m_Button->screenRect(), m_Button->edge(), buildStreamMenu(state));
}

void StreamMenuController::closeMenu(bool restoreCapture)
{
    if (m_Menu) {
        m_Menu->close();
    }
    if (m_Button) {
        m_Button->setKeepVisible(false);
    }
    if (m_PointerLockSuspended) {
        m_Host->suspendPointerLock(false);
        m_PointerLockSuspended = false;
    }
    if (m_CaptureReleased && restoreCapture) {
        m_Host->setCaptureActive(true);
    }
    m_CaptureReleased = false;
}

void StreamMenuController::handleUserEvent(const SDL_UserEvent& event)
{
    switch (event.code) {
    case SDL_CODE_TOGGLE_STREAM_MENU:
        toggleMenu();
        break;
    case SDL_CODE_STREAM_MENU_ACTION:
        onAction((StreamAction)(intptr_t)event.data1);
        break;
    case SDL_CODE_STREAM_MENU_DISMISSED:
        onDismissed();
        break;
    default:
        break;
    }
}

void StreamMenuController::onAction(StreamAction action)
{
    if (action < (StreamAction)0 || action >= StreamAction::Count) {
        return;
    }

    // The menu window has already closed itself.
    const bool leaving = action == StreamAction::Disconnect || action == StreamAction::DisconnectAndExit;
    closeMenu(!leaving);

    m_Host->performAction(action);

    // Full screen / window changes move the stream area
    if (m_Button) {
        m_Button->reposition();
    }
}

void StreamMenuController::onDismissed()
{
    closeMenu(true);
}

bool StreamMenuController::handleKeyEvent(const SDL_KeyboardEvent& event)
{
    if (!isMenuOpen()) {
        return false;
    }

    // Releases always go to the PC, so no key can stay pressed there.
    if (event.type != SDL_KEYDOWN) {
        return false;
    }

    const Uint16 mod = event.keysym.mod;
    if ((mod & KMOD_CTRL) && (mod & KMOD_ALT) && (mod & KMOD_SHIFT)) {
        return false;
    }

    // Auto-repeat (e.g. Space still held after Ctrl+Alt+Shift+Space) never activates.
    if (event.repeat) {
        return true;
    }

    switch (event.keysym.scancode) {
    case SDL_SCANCODE_ESCAPE:
        closeMenu(true);
        break;
    case SDL_SCANCODE_UP:
        m_Menu->moveHighlight(-1);
        break;
    case SDL_SCANCODE_DOWN:
        m_Menu->moveHighlight(+1);
        break;
    case SDL_SCANCODE_TAB:
        m_Menu->moveHighlight((mod & KMOD_SHIFT) ? -1 : +1);
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_SPACE:
        m_Menu->activateHighlighted();
        break;
    default:
        break;
    }
    return true;
}

void StreamMenuController::showToast(const QString& text, int durationMs)
{
    if (!m_Toast || !m_Button) {
        return;
    }
    RECT rc;
    GetClientRect(m_Hwnd, &rc);
    POINT tl = { 0, 0 };
    ClientToScreen(m_Hwnd, &tl);
    const QRect area(tl.x, tl.y, rc.right - rc.left, rc.bottom - rc.top);
    m_Toast->show(text, m_Button->screenRect(), area, durationMs);
}

void StreamMenuController::setAutoHideButton(bool autoHide)
{
    if (m_Button) {
        m_Button->setAutoHide(autoHide);
    }
}

void StreamMenuController::setCursorProviderForTests(std::function<QPoint()> provider)
{
    if (m_Button) {
        m_Button->setCursorProvider(std::move(provider));
    }
}

void StreamMenuController::handleWindowEvent(const SDL_WindowEvent& event)
{
    if (!m_Button) {
        return;
    }

    switch (event.event) {
    case SDL_WINDOWEVENT_MINIMIZED:
    case SDL_WINDOWEVENT_HIDDEN:
        closeMenu(false);
        m_Button->setVisible(false);
        break;
    case SDL_WINDOWEVENT_RESTORED:
    case SDL_WINDOWEVENT_SHOWN:
    case SDL_WINDOWEVENT_MAXIMIZED:
        m_Button->setVisible(true);
        break;
    case SDL_WINDOWEVENT_MOVED:
    case SDL_WINDOWEVENT_RESIZED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
#if SDL_VERSION_ATLEAST(2, 0, 18)
    case SDL_WINDOWEVENT_DISPLAY_CHANGED:
#endif
        // Full screen <-> window shows up as a size change
        m_Button->setAutoHide((SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN) != 0);
        m_Button->reposition();
        break;
    default:
        break;
    }
}

}
