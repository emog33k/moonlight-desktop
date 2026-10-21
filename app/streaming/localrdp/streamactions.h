#pragma once

// Actions the in-stream menu (and the Ctrl+Alt+Shift shortcuts) can perform,
// and the session state the menu shows. Pure data: no SDL or Windows headers.

namespace LocalRdp {

enum class StreamAction {
    SetDesktopMouseMode,
    SetGameMouseMode,
    ToggleLocalCursor,
    TogglePointerLock,
    ToggleHostCursor,
    ToggleSystemKeyCapture,
    PasteClipboardText,
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

// Connection to LocalRDP Agent on the PC (PC lock, later clipboard and files).
enum class AgentStatus { Unknown, Connecting, Connected, NotFound, CertError, Rejected };

struct StreamState {
    bool absoluteMouse = true;
    bool localCursorVisible = false;
    bool pointerLocked = false;
    bool systemKeyCapture = true;
    bool fullscreen = false;
    bool statsVisible = false;
    AgentStatus agentStatus = AgentStatus::Unknown;
    bool lockOnDisconnect = false;
    bool clipboardSync = true;
};

}
