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
    virtual void performAction(StreamAction action) = 0;

    // Mouse capture (game mode hides and locks the cursor); the menu releases it while open.
    virtual bool isCaptureActive() = 0;
    virtual void setCaptureActive(bool active) = 0;
};

}
