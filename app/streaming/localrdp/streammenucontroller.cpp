#include "streammenucontroller.h"
#include "streammenu_win32.h"
#include "streammenumodel.h"
#include "toast_win32.h"
#include "toolbar_win32.h"
#include "videoarea.h"
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

static void postUserEvent(int code, int data = 0, int data2 = 0)
{
    SDL_Event event = {};
    event.type = SDL_USEREVENT;
    event.user.code = code;
    event.user.data1 = (void*)(intptr_t)data;
    event.user.data2 = (void*)(intptr_t)data2;
    SDL_PushEvent(&event);
}

static QRect clientScreenRect(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    POINT tl = { 0, 0 };
    ClientToScreen(hwnd, &tl);
    return QRect(tl.x, tl.y, rc.right - rc.left, rc.bottom - rc.top);
}

StreamMenuController::StreamMenuController(SDL_Window* window, StreamMenuHost* host)
    : m_Window(window),
      m_Host(host),
      m_Gdiplus(new GdiplusSession())
{
    m_Hwnd = hwndForWindow(window);
    if (m_Hwnd == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "LocalRDP: no native window handle, in-stream toolbar disabled");
        return;
    }

    // Window procedures run inside SDL's event pump, so they only post events;
    // the session loop calls back into handleUserEvent().
    m_Toolbar.reset(new ToolbarWindow(m_Hwnd, [this](int index) {
        // Bring the stream forward so the menu's mouse capture and the keyboard
        // (Esc, arrows) work even if another app was active. We just received
        // the click, so Windows allows this.
        if (m_BringToFrontOnClick && GetForegroundWindow() != m_Hwnd &&
                m_Toolbar->buttons()[index].kind == ToolbarButton::Kind::Section) {
            SetForegroundWindow(m_Hwnd);
        }
        postUserEvent(SDL_CODE_LOCALRDP_TOOLBAR, index, 0);
    }));
    m_Toolbar->setStatusProvider([host]() { return host->queryState().agentStatus; });

    m_Menu.reset(new StreamMenuWindow(m_Hwnd,
                                      [](StreamAction action, int param) { postUserEvent(SDL_CODE_STREAM_MENU_ACTION, (int)action, param); },
                                      []() { postUserEvent(SDL_CODE_STREAM_MENU_DISMISSED); }));
    m_Menu->setOutsideHandler([this](QPoint screen, bool press) { return onMenuOutside(screen, press); });
    m_Toast.reset(new ToastWindow(m_Hwnd));

    updateLayout();
}

StreamMenuController::~StreamMenuController()
{
    // Windows must go before GDI+ shuts down. The stream is ending, so the
    // capture we may have released is not given back.
    m_Toast.reset();
    m_Menu.reset();
    m_Toolbar.reset();
    m_Gdiplus.reset();
    setVideoTopInset(0);
}

bool StreamMenuController::isMenuOpen() const
{
    return m_Menu && m_Menu->isOpen();
}

void StreamMenuController::toggleMenu()
{
    if (m_MenuButton != kNoMenu || isMenuOpen()) {
        closeMenu(true);
    }
    else {
        showMenu(kFullMenu);
    }
}

void StreamMenuController::showMenu(int button)
{
    if (!m_Toolbar || !m_Menu) {
        return;
    }
    const StreamState state = m_Host->queryState();

    // In game mode the cursor is hidden and locked; free it while the menu is up.
    if (!m_CaptureReleased && !state.absoluteMouse && m_Host->isCaptureActive()) {
        m_Host->setCaptureActive(false);
        m_CaptureReleased = true;
    }
    // A cursor clipped to the window would not reach every menu item.
    if (!m_PointerLockSuspended && state.pointerLocked) {
        m_Host->suspendPointerLock(true);
        m_PointerLockSuspended = true;
    }

    // In full screen this also brings the toolbar out
    m_Toolbar->setKeepVisible(true);

    QRect anchor;
    QVector<MenuItem> items;
    if (button >= 0 && button < m_Toolbar->buttons().size()) {
        anchor = m_Toolbar->buttonScreenRect(button);
        items = buildSectionMenu(m_Toolbar->buttons()[button].section, state);
    }
    if (anchor.isEmpty()) {
        button = kFullMenu;
        const QRect client = clientScreenRect(m_Hwnd);
        const int pad = (int)(4 * dpiScaleForWindow(m_Hwnd) + 0.5);
        anchor = QRect(client.x() + pad, client.y(), 1, m_Toolbar->isShown() ? m_Toolbar->height() : 1);
        items = buildStreamMenu(state);
    }

    m_Toolbar->setActiveButton(button >= 0 ? button : -1);
    m_Menu->show(anchor, Edge::Top, items, 1);
    m_MenuButton = button;
    m_LastHoverPosted = button;
}

void StreamMenuController::closeMenu(bool restoreCapture)
{
    if (m_Menu) {
        m_Menu->close();
    }
    if (m_Toolbar) {
        m_Toolbar->setActiveButton(-1);
        m_Toolbar->setKeepVisible(false);
    }
    m_MenuButton = kNoMenu;
    m_LastHoverPosted = -1;
    if (m_PointerLockSuspended) {
        m_Host->suspendPointerLock(false);
        m_PointerLockSuspended = false;
    }
    if (m_CaptureReleased && restoreCapture) {
        m_Host->setCaptureActive(true);
    }
    m_CaptureReleased = false;
}

bool StreamMenuController::onMenuOutside(QPoint screen, bool press)
{
    // Runs inside the menu's window procedure: only post.
    if (!m_Toolbar) {
        return false;
    }
    const int index = m_Toolbar->buttonAtScreen(screen);
    if (press) {
        if (index < 0) {
            return false; // an ordinary click outside: the menu dismisses itself
        }
        postUserEvent(SDL_CODE_LOCALRDP_TOOLBAR, index, 0);
        return true;
    }
    if (index != m_LastHoverPosted) {
        m_LastHoverPosted = index;
        if (index >= 0 && m_Toolbar->buttons()[index].kind == ToolbarButton::Kind::Section) {
            postUserEvent(SDL_CODE_LOCALRDP_TOOLBAR, index, 1);
        }
    }
    return false;
}

void StreamMenuController::onToolbar(int button, bool hover)
{
    if (!m_Toolbar || button < 0 || button >= m_Toolbar->buttons().size()) {
        return;
    }
    const ToolbarButton& b = m_Toolbar->buttons()[button];
    if (b.kind == ToolbarButton::Kind::Action) {
        if (!hover) {
            onAction(b.action, 0);
        }
        return;
    }
    if (hover) {
        // Moving along the toolbar with a dropdown open switches to that one
        if (m_MenuButton != kNoMenu && m_MenuButton != button) {
            showMenu(button);
        }
        return;
    }
    if (m_MenuButton == button) {
        closeMenu(true);
    }
    else {
        showMenu(button);
    }
}

void StreamMenuController::handleUserEvent(const SDL_UserEvent& event)
{
    switch (event.code) {
    case SDL_CODE_TOGGLE_STREAM_MENU:
        toggleMenu();
        break;
    case SDL_CODE_STREAM_MENU_ACTION:
        onAction((StreamAction)(intptr_t)event.data1, (int)(intptr_t)event.data2);
        break;
    case SDL_CODE_STREAM_MENU_DISMISSED:
        onDismissed();
        break;
    case SDL_CODE_LOCALRDP_TOOLBAR:
        onToolbar((int)(intptr_t)event.data1, (intptr_t)event.data2 != 0);
        break;
    default:
        break;
    }
}

void StreamMenuController::onAction(StreamAction action, int param)
{
    if (action < (StreamAction)0 || action >= StreamAction::Count) {
        return;
    }

    // The menu window has already closed itself.
    const bool leaving = action == StreamAction::Disconnect || action == StreamAction::DisconnectAndExit;
    closeMenu(!leaving);

    m_Host->performAction(action, param);

    // Full screen / window changes move the stream area
    updateLayout();
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
    if (!m_Toast || !m_Toolbar) {
        return;
    }
    const QRect area = clientScreenRect(m_Hwnd);
    const int below = (m_Toolbar->isShown() ? m_Toolbar->height() : 0) +
                      (int)(10 * dpiScaleForWindow(m_Hwnd) + 0.5);
    m_Toast->show(text, QPoint(area.center().x(), area.top() + below), area, durationMs);
}

void StreamMenuController::setFullscreenLayout(bool fullscreen)
{
    if (!m_Toolbar) {
        return;
    }
    m_Toolbar->setFullscreen(fullscreen);
    // In a window the video goes below the toolbar; in full screen it uses the
    // whole screen and the toolbar slides over it on demand.
    setVideoTopInset(fullscreen ? 0 : m_Toolbar->height());
    m_Toolbar->reposition();
}

void StreamMenuController::updateLayout()
{
    if (!m_Toolbar) {
        return;
    }
    setFullscreenLayout((SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN) != 0);
}

void StreamMenuController::setCursorProviderForTests(std::function<QPoint()> provider)
{
    if (m_Toolbar) {
        m_Toolbar->setCursorProvider(std::move(provider));
    }
}

QRect StreamMenuController::toolbarButtonRect(int index) const
{
    if (!m_Toolbar || !m_Toolbar->isShown()) {
        return QRect();
    }
    return m_Toolbar->buttonScreenRect(index);
}

void StreamMenuController::handleWindowEvent(const SDL_WindowEvent& event)
{
    if (!m_Toolbar) {
        return;
    }

    switch (event.event) {
    case SDL_WINDOWEVENT_MINIMIZED:
    case SDL_WINDOWEVENT_HIDDEN:
        closeMenu(false);
        m_Toolbar->setVisible(false);
        break;
    case SDL_WINDOWEVENT_RESTORED:
    case SDL_WINDOWEVENT_SHOWN:
    case SDL_WINDOWEVENT_MAXIMIZED:
        m_Toolbar->setVisible(true);
        updateLayout();
        break;
    case SDL_WINDOWEVENT_MOVED:
    case SDL_WINDOWEVENT_RESIZED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
#if SDL_VERSION_ATLEAST(2, 0, 18)
    case SDL_WINDOWEVENT_DISPLAY_CHANGED:
#endif
        // Full screen <-> window shows up as a size change
        updateLayout();
        break;
    default:
        break;
    }
}

}
