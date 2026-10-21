#pragma once

#include "SDL_compat.h"
#include "streammenuhost.h"

#include <memory>

namespace LocalRdp {

class FloatingButtonWindow;
class GdiplusSession;
class StreamMenuWindow;

// Owns the floating button and the menu for one stream window.
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

    // While the menu is open it uses Esc, arrows, Enter/Space and swallows other
    // keys so they don't reach the PC. Returns true if the event was used here.
    // Ctrl+Alt+Shift combos always pass through (the menu hotkey among them).
    bool handleKeyEvent(const SDL_KeyboardEvent& event);

    // Opens the menu next to the button, or closes it if it is open.
    void toggleMenu();
    bool isMenuOpen() const;

private:
    void openMenu();
    void closeMenu(bool restoreCapture);
    void onAction(StreamAction action);
    void onDismissed();

    SDL_Window* m_Window;
    StreamMenuHost* m_Host;
    std::unique_ptr<GdiplusSession> m_Gdiplus;
    std::unique_ptr<FloatingButtonWindow> m_Button;
    std::unique_ptr<StreamMenuWindow> m_Menu;
    // We released the game-mode mouse capture to show the menu and owe it back.
    bool m_CaptureReleased = false;
};

}
