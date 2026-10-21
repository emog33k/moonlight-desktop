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
    Count // not an action: number of actions
};

struct StreamState {
    bool absoluteMouse = true;
    bool localCursorVisible = false;
    bool pointerLocked = false;
    bool systemKeyCapture = true;
    bool fullscreen = false;
    bool statsVisible = false;
};

}
