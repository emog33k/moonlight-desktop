#pragma once

#include "cursorsync.h"

#include "SDL_compat.h"

#include <QHash>

#include <functional>

namespace LocalRdp {

// Shows the PC's cursor as the SDL (local) cursor. Main thread only.
class SdlCursorHost : public CursorSync::Host
{
public:
    // toggleSunshine: send Ctrl+Alt+Shift+N; setVisibility: the input handler's
    // local cursor visibility override over the video (-1 = its own setting).
    SdlCursorHost(std::function<void()> toggleSunshine, std::function<void(int)> setVisibility);
    ~SdlCursorHost() override;

    SdlCursorHost(const SdlCursorHost&) = delete;
    SdlCursorHost& operator=(const SdlCursorHost&) = delete;

    void toggleSunshineCursor() override;
    void showPcCursor(const Proto::CursorState& cursor) override;
    void restoreLocalCursor() override;

private:
    SDL_Cursor* cursorFor(const Proto::CursorImage& image);
    void setCursor(SDL_Cursor* cursor);

    std::function<void()> m_ToggleSunshine;
    std::function<void(int)> m_SetVisibility;
    QHash<QByteArray, SDL_Cursor*> m_Cache; // by image content
    SDL_Cursor* m_Arrow = nullptr;
    SDL_Cursor* m_Current = nullptr;
};

}
