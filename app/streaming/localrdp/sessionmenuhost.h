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

private:
    StreamMenuHost* m_Input;
    StreamingPreferences* m_Prefs;
    AgentClient* m_Agent;
    std::function<void()> m_PickFilesToSend;
    int m_CurrentDisplay = -1;
    std::function<void(bool)> m_OnCursorShapesChanged;
};

}
