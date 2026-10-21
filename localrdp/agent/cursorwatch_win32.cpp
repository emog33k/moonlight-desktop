#include "cursorwatch_win32.h"

#include <QVector>

#include <cstring>

namespace LocalRdp {

using namespace Proto;

// Draws the cursor onto a solid background into 32-bit top-down pixels.
static bool drawOn(HCURSOR cursor, int w, int h, BYTE background, QVector<quint32>& out)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (dc != nullptr && dib != nullptr && bits != nullptr) {
        HGDIOBJ old = SelectObject(dc, dib);
        std::memset(bits, background, (size_t)w * h * 4);
        ok = DrawIconEx(dc, 0, 0, cursor, w, h, 0, nullptr, DI_NORMAL) != FALSE;
        GdiFlush();
        if (ok) {
            out.resize(w * h);
            std::memcpy(out.data(), bits, (size_t)w * h * 4);
        }
        SelectObject(dc, old);
    }
    if (dib != nullptr) {
        DeleteObject(dib);
    }
    if (dc != nullptr) {
        DeleteDC(dc);
    }
    ReleaseDC(nullptr, screen);
    return ok;
}

CursorImage captureCursorImage(HCURSOR cursor)
{
    CursorImage image;
    ICONINFO info = {};
    if (cursor == nullptr || !GetIconInfo(cursor, &info)) {
        return image;
    }

    int w = 0, h = 0;
    BITMAP bm = {};
    if (info.hbmColor != nullptr && GetObjectW(info.hbmColor, sizeof(bm), &bm)) {
        w = bm.bmWidth;
        h = bm.bmHeight;
    }
    else if (info.hbmMask != nullptr && GetObjectW(info.hbmMask, sizeof(bm), &bm)) {
        // Monochrome cursor: AND mask on top of XOR mask
        w = bm.bmWidth;
        h = bm.bmHeight / 2;
    }
    const int hotX = (int)info.xHotspot, hotY = (int)info.yHotspot;
    if (info.hbmColor != nullptr) {
        DeleteObject(info.hbmColor);
    }
    if (info.hbmMask != nullptr) {
        DeleteObject(info.hbmMask);
    }
    if (w < 1 || h < 1 || w > kMaxCursorSide || h > kMaxCursorSide) {
        return image;
    }

    // On black a pixel shows as color*alpha, on white as color*alpha + 255*(1-alpha):
    // that gives alpha and color back. A pixel brighter on black than on white
    // inverts the screen (the I-beam).
    QVector<quint32> onBlack, onWhite;
    if (!drawOn(cursor, w, h, 0x00, onBlack) || !drawOn(cursor, w, h, 0xFF, onWhite)) {
        return image;
    }

    QVector<quint8> invert(w * h, 0);
    image.bgra.resize(w * h * 4);
    uchar* px = (uchar*)image.bgra.data();
    for (int i = 0; i < w * h; i++) {
        const quint32 b = onBlack[i], wh = onWhite[i];
        int alpha = 0;
        bool inverts = false;
        for (int c = 0; c < 3; c++) {
            const int vb = (b >> (8 * c)) & 0xFF, vw = (wh >> (8 * c)) & 0xFF;
            if (vb > vw) {
                inverts = true;
            }
            alpha = qMax(alpha, 255 - (vw - vb));
        }
        alpha = qBound(0, alpha, 255);
        if (inverts) {
            invert[i] = 1;
            px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = 0;
            px[i * 4 + 3] = 255;
            continue;
        }
        for (int c = 0; c < 3; c++) {
            const int vb = (b >> (8 * c)) & 0xFF;
            px[i * 4 + c] = alpha > 0 ? (uchar)qMin(255, vb * 255 / alpha) : 0;
        }
        px[i * 4 + 3] = (uchar)alpha;
    }

    // A light outline around inverting parts keeps them visible on dark content
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int i = y * w + x;
            if (px[i * 4 + 3] != 0) {
                continue;
            }
            bool nearInvert = false;
            for (int dy = -1; dy <= 1 && !nearInvert; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < w && ny < h && invert[ny * w + nx]) {
                        nearInvert = true;
                        break;
                    }
                }
            }
            if (nearInvert) {
                px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = 255;
                px[i * 4 + 3] = 255;
            }
        }
    }

    image.width = w;
    image.height = h;
    image.hotX = qBound(0, hotX, w - 1);
    image.hotY = qBound(0, hotY, h - 1);
    return image;
}

CursorState CursorWatcher::read()
{
    HCURSOR cursor = nullptr;
    bool visible = false;
    bool known;
    if (m_Source) {
        known = m_Source(&cursor, &visible);
    }
    else {
        CURSORINFO ci = { sizeof(ci) };
        known = GetCursorInfo(&ci) != FALSE;
        cursor = ci.hCursor;
        // CURSOR_SUPPRESSED (touch/pen input) counts as hidden
        visible = (ci.flags & CURSOR_SHOWING) != 0;
    }

    CursorState state;
    if (!known) {
        return state; // Default: a plain arrow on the laptop
    }
    if (!visible || cursor == nullptr) {
        state.kind = CursorKind::Hidden;
        return state;
    }

    if (cursor != m_Cursor) {
        m_Cursor = cursor;
        m_Shape = CursorState();
        m_Shape.image = captureCursorImage(cursor);
        m_Shape.kind = m_Shape.image.width > 0 ? CursorKind::Shape : CursorKind::Default;
        m_Shape.key = (quint64)(quintptr)cursor;
    }
    return m_Shape;
}

}
