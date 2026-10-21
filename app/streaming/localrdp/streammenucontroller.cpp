#include "streammenucontroller.h"
#include "floatingbutton_win32.h"
#include "streammenu_win32.h"
#include "streammenumodel.h"
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

StreamMenuController::StreamMenuController(SDL_Window* window, StreamMenuHost* host)
    : m_Window(window),
      m_Host(host),
      m_Gdiplus(new GdiplusSession())
{
    HWND hwnd = hwndForWindow(window);
    if (hwnd == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "LocalRDP: no native window handle, in-stream menu disabled");
        return;
    }
    m_Button.reset(new FloatingButtonWindow(hwnd, [this]() { toggleMenu(); }));
    m_Menu.reset(new StreamMenuWindow(hwnd,
                                      [this](StreamAction action) { onAction(action); },
                                      [this]() { onDismissed(); }));
}

StreamMenuController::~StreamMenuController()
{
    // Windows must go before GDI+ shuts down. The stream is ending, so the
    // capture we may have released is not given back.
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

    m_Menu->show(m_Button->screenRect(), m_Button->edge(), buildStreamMenu(state));
}

void StreamMenuController::closeMenu(bool restoreCapture)
{
    if (m_Menu) {
        m_Menu->close();
    }
    if (m_CaptureReleased && restoreCapture) {
        m_Host->setCaptureActive(true);
    }
    m_CaptureReleased = false;
}

void StreamMenuController::onAction(StreamAction action)
{
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

    const Uint16 mod = event.keysym.mod;
    if ((mod & KMOD_CTRL) && (mod & KMOD_ALT) && (mod & KMOD_SHIFT)) {
        return false;
    }

    if (event.type == SDL_KEYDOWN) {
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
    }
    return true;
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
        m_Button->reposition();
        break;
    default:
        break;
    }
}

}
