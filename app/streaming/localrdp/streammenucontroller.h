#pragma once

#include "SDL_compat.h"
#include "streammenuhost.h"

#include <QPoint>
#include <QRect>
#include <QString>

#include <functional>
#include <memory>

#include "streamevents.h"

struct HWND__;

namespace LocalRdp {

class GdiplusSession;
class StreamMenuWindow;
class ToastWindow;
class ToolbarWindow;

// Owns the toolbar, its dropdown menu and the notices for one stream window.
// Lives on the session's SDL thread; create after the SDL window exists and
// destroy before the SDL window is destroyed.
//
// In a window the toolbar sits in a strip above the video (the renderers and
// the mouse code leave that strip out, see videoarea.h); in full screen it
// hides and shows at the top edge of the screen.
class StreamMenuController
{
public:
    StreamMenuController(SDL_Window* window, StreamMenuHost* host);
    ~StreamMenuController();

    StreamMenuController(const StreamMenuController&) = delete;
    StreamMenuController& operator=(const StreamMenuController&) = delete;

    void handleWindowEvent(const SDL_WindowEvent& event);

    // While a menu is open it uses Esc, arrows, Enter/Space and swallows other
    // keys so they don't reach the PC. Returns true if the event was used here.
    // Ctrl+Alt+Shift combos always pass through (the menu hotkey among them).
    bool handleKeyEvent(const SDL_KeyboardEvent& event);

    // Hotkey: opens the full menu under the toolbar, or closes any open menu.
    void toggleMenu();
    bool isMenuOpen() const;

    // Session loop hands back the SDL_CODE_TOGGLE_STREAM_MENU / SDL_CODE_STREAM_MENU_* /
    // SDL_CODE_LOCALRDP_TOOLBAR events.
    void handleUserEvent(const SDL_UserEvent& event);

    // A short notice under the toolbar (e.g. after sending files to the PC).
    void showToast(const QString& text, int durationMs = 4000);
    // Pressing a toolbar section brings the stream window to the front (on by
    // default; UI tests turn it off so they never take focus from the user).
    void setBringToFrontOnClick(bool enabled) { m_BringToFrontOnClick = enabled; }

    // Full screen layout (the controller follows the SDL window by itself; tests
    // use this to avoid covering the screen).
    void setFullscreenLayout(bool fullscreen);
    void setCursorProviderForTests(std::function<QPoint()> provider);
    // Screen rect of a toolbar button (index into toolbarButtons()); empty if hidden or no room.
    QRect toolbarButtonRect(int index) const;

private:
    static constexpr int kNoMenu = -2;
    static constexpr int kFullMenu = -1;

    void showMenu(int button);
    void closeMenu(bool restoreCapture);
    void onToolbar(int button, bool hover);
    void onAction(StreamAction action, int param);
    void onDismissed();
    bool onMenuOutside(QPoint screen, bool press);
    void updateLayout();

    SDL_Window* m_Window;
    StreamMenuHost* m_Host;
    HWND__* m_Hwnd = nullptr;
    std::unique_ptr<GdiplusSession> m_Gdiplus;
    std::unique_ptr<ToolbarWindow> m_Toolbar;
    std::unique_ptr<StreamMenuWindow> m_Menu;
    std::unique_ptr<ToastWindow> m_Toast;
    // Which menu is open: kNoMenu, kFullMenu or a toolbar button index.
    int m_MenuButton = kNoMenu;
    // Last toolbar button the pointer moved onto while a menu was open (posted once).
    int m_LastHoverPosted = -1;
    // We released the game-mode mouse capture to show the menu and owe it back.
    bool m_CaptureReleased = false;
    // We lifted "keep the cursor inside the window" while the menu is open.
    bool m_PointerLockSuspended = false;
    bool m_BringToFrontOnClick = true;
};

}
