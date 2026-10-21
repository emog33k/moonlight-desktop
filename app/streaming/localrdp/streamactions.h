#pragma once

// Actions the toolbar menus (and the Ctrl+Alt+Shift shortcuts) can perform, and
// the session state the menus show. Pure data: no SDL or Windows headers.

#include <QString>
#include <QVector>

namespace LocalRdp {

enum class StreamAction {
    SetDesktopMouseMode,
    SetGameMouseMode,
    ToggleLocalCursor,       // Ctrl+Alt+Shift+C only (not in the menus)
    ToggleLocalCursorShapes, // AnyDesk-style cursor: local cursor with the PC's cursor shape
    TogglePointerLock,
    ToggleSystemKeyCapture,
    PasteClipboardText,
    SwitchDisplay,           // param = display index (Sunshine order)
    ToggleFullscreen,
    ToggleStats,
    Minimize,
    Disconnect,
    DisconnectAndExit,
    LockPc,
    ToggleLockOnDisconnect,
    ToggleClipboardSync,
    SendFilesToPc,
    Count // not an action: number of actions
};

// Connection to LocalRDP Agent on the PC.
enum class AgentStatus { Unknown, Connecting, Connected, NotFound, CertError, Rejected };

// A PC monitor as Sunshine numbers them (see the agent's display list).
struct DisplayInfo {
    QString name;
    int width = 0;
    int height = 0;
    bool primary = false;
};

struct StreamState {
    bool absoluteMouse = true;
    bool localCursorVisible = false;
    bool localCursorShapes = true;
    bool pointerLocked = false;
    bool systemKeyCapture = true;
    bool fullscreen = false;
    bool statsVisible = false;
    AgentStatus agentStatus = AgentStatus::Unknown;
    bool lockOnDisconnect = false;
    bool clipboardSync = true;
    QVector<DisplayInfo> displays; // empty: unknown (no agent)
    int currentDisplay = -1;       // -1: not known yet
};

}
