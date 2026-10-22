#include "videoarea.h"

#include <atomic>
#include <cmath>

namespace LocalRdp {

static std::atomic<int> s_TopInset { 0 };

void setVideoTopInset(int pixels)
{
    s_TopInset = pixels > 0 ? pixels : 0;
}

int videoTopInset()
{
    return s_TopInset;
}

VideoRect toBottomUp(const VideoRect& rect, int windowH)
{
    VideoRect r = rect;
    r.y = windowH - (rect.y + rect.h);
    return r;
}

VideoRect videoArea(int windowW, int windowH, int streamW, int streamH, int topInset)
{
    VideoRect area;
    area.x = 0;
    area.y = topInset > 0 ? topInset : 0;
    area.w = windowW > 0 ? windowW : 0;
    area.h = windowH - area.y > 0 ? windowH - area.y : 0;
    if (area.w == 0 || area.h == 0) {
        area.w = area.h = 0;
        return area;
    }
    if (streamW <= 0 || streamH <= 0) {
        return area;
    }

    const int fitH = (int)std::ceil((float)area.w * streamH / streamW);
    const int fitW = (int)std::ceil((float)area.h * streamW / streamH);
    if (fitH > area.h) {
        area.x += (area.w - fitW) / 2;
        area.w = fitW;
    }
    else {
        area.y += (area.h - fitH) / 2;
        area.h = fitH;
    }
    return area;
}

}
