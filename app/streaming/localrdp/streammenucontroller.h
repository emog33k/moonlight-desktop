#pragma once

#include "SDL_compat.h"
#include "streammenuhost.h"

#include <QPoint>
#include <QString>

#include <functional>
#include <memory>

#include "streamevents.h"

struct HWND__;

namespace LocalRdp {

class FloatingButtonWindow;
class GdiplusSession;
class StreamMenuWindow;
class ToastWindow;

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

    // Session loop hands back SDL_CODE_TOGGLE_STREAM_MENU / SDL_CODE_STREAM_MENU_* events.
    void handleUserEvent(const SDL_UserEvent& event);

    // In full screen the button hides until the pointer comes near it, so no
    // overlay sits on top of the stream (keeps DWM independent flip possible).
    void setAutoHideButton(bool autoHide);
    void setCursorProviderForTests(std::function<QPoint()> provider);

    // A short notice next to the button (e.g. after sending files to the PC).
    void showToast(const QString& text, int durationMs = 4000);
    // Clicking the button brings the stream window to the front (on by default;
    // UI tests turn it off so they never take focus from the user).
    void setBringToFrontOnClick(bool enabled) { m_BringToFrontOnClick = enabled; }

private:
    void openMenu();
    void closeMenu(bool restoreCapture);
    void onAction(StreamAction action, int param);
    void onDismissed();

    SDL_Window* m_Window;
    StreamMenuHost* m_Host;
    HWND__* m_Hwnd = nullptr;
    std::unique_ptr<GdiplusSession> m_Gdiplus;
    std::unique_ptr<FloatingButtonWindow> m_Button;
    std::unique_ptr<StreamMenuWindow> m_Menu;
    std::unique_ptr<ToastWindow> m_Toast;
    // We released the game-mode mouse capture to show the menu and owe it back.
    bool m_CaptureReleased = false;
    // We lifted "keep the cursor inside the window" while the menu is open.
    bool m_PointerLockSuspended = false;
    bool m_BringToFrontOnClick = true;
};

}
