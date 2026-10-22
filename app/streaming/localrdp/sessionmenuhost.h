#pragma once

#include "streammenuhost.h"

#include <functional>

class StreamingPreferences;

namespace LocalRdp {

class AgentClient;

// What the in-stream menu talks to during a session: input actions go to the
// input handler, PC actions to LocalRDP Agent (if connected), and the
// "lock on disconnect" preference is kept here.
class SessionMenuHost : public StreamMenuHost
{
public:
    // agent may be null (no agent client for this stream). pickFilesToSend opens the
    // "send files to PC" file dialog (provided by the session).
    SessionMenuHost(StreamMenuHost* input, StreamingPreferences* prefs, AgentClient* agent,
                    std::function<void()> pickFilesToSend = {});

    StreamState queryState() override;
    void performAction(StreamAction action, int param = 0) override;
    bool isCaptureActive() override;
    void setCaptureActive(bool active) override;
    void suspendPointerLock(bool suspended) override;

    // The session applies the AnyDesk-style cursor setting when it changes.
    void setCursorShapesChangedHandler(std::function<void(bool)> handler) { m_OnCursorShapesChanged = std::move(handler); }
    // Opens the laptop's "snip an area" (the session provides it)
    void setLaptopSnipLauncher(std::function<void()> launcher) { m_LaunchLaptopSnip = std::move(launcher); }
    // A short notice for the user (the session shows it under the toolbar)
    void setToastHandler(std::function<void(const QString&)> handler) { m_Toast = std::move(handler); }
    // The AnyDesk-style cursor: whether it is on now, and its "two cursors" fix
    void setCursorSyncActiveProvider(std::function<bool()> provider) { m_CursorSyncActive = std::move(provider); }
    void setFixDoubleCursorHandler(std::function<void()> handler) { m_FixDoubleCursor = std::move(handler); }
    // Ends this stream and starts it again (new quality settings take effect)
    void setRestartHandler(std::function<void()> handler) { m_Restart = std::move(handler); }

private:
    StreamMenuHost* m_Input;
    StreamingPreferences* m_Prefs;
    AgentClient* m_Agent;
    std::function<void()> m_PickFilesToSend;
    int m_CurrentDisplay = -1;
    std::function<void(bool)> m_OnCursorShapesChanged;
    std::function<void()> m_LaunchLaptopSnip;
    std::function<bool()> m_CursorSyncActive;
    std::function<void()> m_FixDoubleCursor;
    std::function<void()> m_Restart;
    std::function<void(const QString&)> m_Toast;
    PowerConfirm m_PowerConfirm = PowerConfirm::None;
    qint64 m_PowerConfirmSince = 0; // the confirmation is valid for a few seconds
    void toast(const QString& text) { if (m_Toast) m_Toast(text); }
};

}
