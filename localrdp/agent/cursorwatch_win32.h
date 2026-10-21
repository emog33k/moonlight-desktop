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
// visible. Width 0 if the cursor cannot be drawn or is larger than kMaxCursorSide.
Proto::CursorImage captureCursorImage(HCURSOR cursor);

// The PC's current cursor (GetCursorInfo). Cheap to call often: the image is
// only drawn again when the cursor changes.
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
    Proto::CursorState m_Shape;
};

}
