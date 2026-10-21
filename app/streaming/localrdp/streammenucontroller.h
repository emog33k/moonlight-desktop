#pragma once

#include "SDL_compat.h"
#include "streammenuhost.h"

#include <memory>

namespace LocalRdp {

class FloatingButtonWindow;
class GdiplusSession;

// Owns the floating button (and, later, the menu) for one stream window.
// Lives on the session's SDL thread; create after the SDL window exists and
// destroy before the SDL window is destroyed.
class StreamMenuController
{
public:
    StreamMenuController(SDL_Window* window, StreamMenuHost* host);
    ~StreamMenuController();

    StreamMenuController(const StreamMenuController&) = delete;
    StreamMenuController& operator=(const StreamMenuController&) = delete;

    void handleWindowEvent(const SDL_WindowEvent& event);

    // Opens the menu next to the button, or closes it if it is open.
    void toggleMenu();

private:
    SDL_Window* m_Window;
    StreamMenuHost* m_Host;
    std::unique_ptr<GdiplusSession> m_Gdiplus;
    std::unique_ptr<FloatingButtonWindow> m_Button;
};

}
