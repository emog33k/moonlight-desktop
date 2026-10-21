#pragma once

#include "streaming/localrdp/streammenuhost.h"
#include "SDL_compat.h"

namespace LocalRdp { class StreamMenuController; }

#include <QObject>
#include <QRect>
#include <QTemporaryDir>
#include <QVector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Records what the menu asked the stream to do.
class FakeHost : public LocalRdp::StreamMenuHost
{
public:
    LocalRdp::StreamState state;
    QVector<LocalRdp::StreamAction> performed;
    bool captureActive = false;
    QVector<bool> captureChanges;

    LocalRdp::StreamState queryState() override { return state; }
    void performAction(LocalRdp::StreamAction action) override { performed << action; }
    bool isCaptureActive() override { return captureActive; }
    void setCaptureActive(bool active) override { captureActive = active; captureChanges << active; }
    QVector<bool> pointerLockSuspends;
    void suspendPointerLock(bool suspended) override { pointerLockSuspends << suspended; }
};

// Runs menu actions the way the session loop does: SDL_USEREVENTs posted by the
// controller are handed back to it.
void deliverMenuEvents(LocalRdp::StreamMenuController& controller, int ms = 100);

// Shared helpers: an SDL window standing in for the stream window.
namespace UiTest {
SDL_Window* createStreamWindow(int x, int y, int w, int h);
HWND hwndOf(SDL_Window* window);
QRect clientScreenRect(HWND hwnd);
QRect windowScreenRect(HWND hwnd);
// Finds a visible-or-hidden top-level window of the given class owned by owner.
HWND findOwnedWindow(HWND owner, const wchar_t* className);
// Lets SDL and Windows process pending messages for a while.
void pump(int ms = 150);
// Sends a mouse message to hwnd at a screen position (converted to its client coordinates).
void sendMouse(HWND hwnd, UINT msg, QPoint screenPos);
// Saves what is on screen inside rect to <exe dir>/results/<name>.png (for a human to look at).
void saveScreenshot(const QRect& rect, const QString& name);
}

class TestFloatingButton : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void defaultSpotRightEdge();
    void followsWindowMoveAndResize();
    void hiddenWhileMinimized();
    void smallMoveIsClick();
    void dragSnapsToEdgeAndPersists();
    void dragDoesNotClick();
    void controllerDestroyRemovesButton();

private:
    QTemporaryDir* m_SettingsDir = nullptr;
    SDL_Window* m_Window = nullptr;
};

namespace LocalRdp { class StreamMenuController; }

class TestStreamMenu : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void opensNextToButton();
    void toggleTwiceCloses();
    void buttonClickOpensMenu();
    void clickItemPerformsActionAndCloses();
    void clickHeaderKeepsOpen();
    void clickOutsideClosesWithoutAction();
    void escapeClosesAndIsConsumed();
    void arrowsAndEnterActivate();
    void menuHotkeyPassesThroughWhenOpen();
    void otherKeysConsumedWhileOpen();
    void gameModeReleasesAndRestoresCapture();
    void disconnectDoesNotRecapture();
    void desktopModeKeepsCapture();
    void minimizeClosesMenu();
    void destroyWhileOpenRemovesEverything();
    void keyUpsPassThroughWhileOpen();
    void repeatedSpaceDoesNotActivate();
    void actionRunsFromEventLoopNotWndProc();
    void pointerLockSuspendedWhileOpen();
    void openingFromBackgroundActivatesStream();
    void buttonAutoHidesInFullscreen();
    void toastShowsThenHides();

private:
    HWND menuHwnd();
    bool menuShown();
    QPoint itemCenterOnScreen(LocalRdp::StreamAction action);
    void click(QPoint screenPos);

    QTemporaryDir* m_SettingsDir = nullptr;
    SDL_Window* m_Window = nullptr;
    HWND m_Owner = nullptr;
    FakeHost* m_Host = nullptr;
    LocalRdp::StreamMenuController* m_Controller = nullptr;
};
