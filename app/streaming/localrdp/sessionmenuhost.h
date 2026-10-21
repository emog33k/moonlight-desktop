#pragma once

#include "streammenuhost.h"

class StreamingPreferences;

namespace LocalRdp {

class AgentClient;

// What the in-stream menu talks to during a session: input actions go to the
// input handler, PC actions to LocalRDP Agent (if connected), and the
// "lock on disconnect" preference is kept here.
class SessionMenuHost : public StreamMenuHost
{
public:
    // agent may be null (no agent client for this stream).
    SessionMenuHost(StreamMenuHost* input, StreamingPreferences* prefs, AgentClient* agent);

    StreamState queryState() override;
    void performAction(StreamAction action) override;
    bool isCaptureActive() override;
    void setCaptureActive(bool active) override;
    void suspendPointerLock(bool suspended) override;

private:
    StreamMenuHost* m_Input;
    StreamingPreferences* m_Prefs;
    AgentClient* m_Agent;
};

}
