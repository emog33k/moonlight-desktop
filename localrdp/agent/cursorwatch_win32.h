#pragma once

#include "localrdp-shared/cursorshape.h"

#include <functional>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace LocalRdp {

// Draws a cursor into straight-alpha BGRA pixels. Cursors that invert the
// screen (the text I-beam) come out black with a light outline so they stay
// visible; ones larger than kMaxCursorSide are scaled down. Width 0 if the
// cursor cannot be drawn.
Proto::CursorImage captureCursorImage(HCURSOR cursor);

// The PC's current cursor (GetCursorInfo). Cheap to call often: the image is
// only drawn again when the cursor changes, and once a second as a check.
class CursorWatcher
{
public:
    Proto::CursorState read();

    // Replaces GetCursorInfo: fills the cursor handle and whether it is shown;
    // false = no information (e.g. the secure desktop).
    void setSourceForTests(std::function<bool(HCURSOR*, bool*)> source) { m_Source = std::move(source); }

private:
    std::function<bool(HCURSOR*, bool*)> m_Source;
    HCURSOR m_Cursor = nullptr;
    qint64 m_CapturedAt = 0;
    Proto::CursorState m_Shape; // key: hash of the picture
};

}
