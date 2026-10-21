#include "streammenucontroller.h"
#include "floatingbutton_win32.h"
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
}

StreamMenuController::~StreamMenuController()
{
    // Windows must go before GDI+ shuts down
    m_Button.reset();
    m_Gdiplus.reset();
}

void StreamMenuController::handleWindowEvent(const SDL_WindowEvent& event)
{
    if (!m_Button) {
        return;
    }

    switch (event.event) {
    case SDL_WINDOWEVENT_MINIMIZED:
    case SDL_WINDOWEVENT_HIDDEN:
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

void StreamMenuController::toggleMenu()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "LocalRDP: menu toggle requested");
}

}
