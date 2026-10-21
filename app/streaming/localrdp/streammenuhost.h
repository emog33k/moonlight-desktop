#pragma once

#include "streamactions.h"

namespace LocalRdp {

// What the in-stream menu needs from the stream. SdlInputHandler implements it
// in the app; UI tests use a fake.
class StreamMenuHost
{
public:
    virtual ~StreamMenuHost() = default;

    virtual StreamState queryState() = 0;
    // param: extra value for some actions (display index for SwitchDisplay).
    virtual void performAction(StreamAction action, int param = 0) = 0;

    // Mouse capture (game mode hides and locks the cursor); the menu releases it while open.
    virtual bool isCaptureActive() = 0;
    virtual void setCaptureActive(bool active) = 0;

    // "Keep the cursor inside the window" would clip the cursor away from parts of
    // the menu; the menu lifts that limit while open (without changing the setting).
    virtual void suspendPointerLock(bool suspended) = 0;
};

}
