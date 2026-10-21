#include "sdlcursor.h"

#include <QCryptographicHash>
#include <QtEndian>

namespace LocalRdp {

using namespace Proto;

static const int kMaxCachedCursors = 64;

SdlCursorHost::SdlCursorHost(std::function<void()> toggleSunshine, std::function<void(int)> setVisibility)
    : m_ToggleSunshine(std::move(toggleSunshine)),
      m_SetVisibility(std::move(setVisibility))
{
}

SdlCursorHost::~SdlCursorHost()
{
    SDL_SetCursor(SDL_GetDefaultCursor());
    for (SDL_Cursor* c : m_Cache) {
        SDL_FreeCursor(c);
    }
    if (m_Arrow != nullptr) {
        SDL_FreeCursor(m_Arrow);
    }
}

void SdlCursorHost::toggleSunshineCursor()
{
    m_ToggleSunshine();
}

SDL_Cursor* SdlCursorHost::cursorFor(const CursorImage& image)
{
    QByteArray key = QCryptographicHash::hash(image.bgra, QCryptographicHash::Sha1);
    key += QByteArray::number(image.width) + 'x' + QByteArray::number(image.height) + '@' +
           QByteArray::number(image.hotX) + ',' + QByteArray::number(image.hotY);
    if (SDL_Cursor* cached = m_Cache.value(key)) {
        return cached;
    }

    if (m_Cache.size() >= kMaxCachedCursors) {
        // Drop everything but the cursor on screen
        for (auto it = m_Cache.begin(); it != m_Cache.end();) {
            if (it.value() != m_Current) {
                SDL_FreeCursor(it.value());
                it = m_Cache.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    // B,G,R,A bytes with straight alpha
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom((void*)image.bgra.constData(), image.width, image.height,
                                                              32, image.width * 4, SDL_PIXELFORMAT_BGRA32);
    if (surface == nullptr) {
        return nullptr;
    }
    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, image.hotX, image.hotY);
    SDL_FreeSurface(surface);
    if (cursor != nullptr) {
        m_Cache.insert(key, cursor);
    }
    return cursor;
}

void SdlCursorHost::setCursor(SDL_Cursor* cursor)
{
    if (cursor != nullptr && cursor != m_Current) {
        SDL_SetCursor(cursor);
        m_Current = cursor;
    }
}

void SdlCursorHost::showPcCursor(const CursorState& cursor)
{
    switch (cursor.kind) {
    case CursorKind::Hidden:
        m_SetVisibility(SDL_DISABLE);
        return;
    case CursorKind::Shape:
        if (SDL_Cursor* c = cursorFor(cursor.image)) {
            setCursor(c);
            break;
        }
        // Could not build it: plain arrow
        Q_FALLTHROUGH();
    case CursorKind::Default:
        if (m_Arrow == nullptr) {
            m_Arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
        }
        setCursor(m_Arrow);
        break;
    }
    m_SetVisibility(SDL_ENABLE);
}

void SdlCursorHost::restoreLocalCursor()
{
    SDL_SetCursor(SDL_GetDefaultCursor());
    m_Current = nullptr;
    m_SetVisibility(-1);
}

}
