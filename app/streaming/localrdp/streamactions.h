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
    SnipLaptop,              // laptop's "snip an area" (the picture syncs to the PC's clipboard)
    SendKeys,                // param = QuickKeys: a key combination on the PC
    FixDoubleCursor,         // Sunshine's cursor still shows next to ours: toggle it once
    ToggleStreamAudio,       // the PC's sound on the laptop on/off (the PC keeps its sound)
    VolumeUp,                // +10 %
    VolumeDown,              // -10 %
    Count // not an action: number of actions
};

// Key combinations the "Действия" menu sends to the PC (see quickKeyCombo()).
enum class QuickKeys {
    Paste,        // Ctrl+V
    SnipArea,     // Win+Shift+S: snip an area of the PC (lands in the laptop's clipboard)
    PrintScreen,  // the PC's whole screen to its clipboard (and so the laptop's)
    TaskManager,  // Ctrl+Shift+Esc
    ShowDesktop,  // Win+D
    Explorer,     // Win+E
    Start,        // Win
    AltTab,       // Alt+Tab
    Count
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
    bool cursorSyncActive = false; // the AnyDesk-style cursor is on right now
    bool streamAudio = true;       // the PC's sound plays on the laptop
    int streamVolume = 100;        // percent
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
