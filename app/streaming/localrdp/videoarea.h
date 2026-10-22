#pragma once

// Where the video goes inside the stream window. Pure logic, unit tested; used by
// the renderers and by the mouse code, so pictures and clicks always agree.

namespace LocalRdp {

struct VideoRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// Space reserved at the top of the window for the toolbar (0 in full screen).
// Set from the stream window's thread; read by renderers and input.
void setVideoTopInset(int pixels);
int videoTopInset();

// The stream (streamW x streamH) fitted into the window below topInset, keeping
// its aspect ratio and centered (same rounding as StreamUtils::scaleSourceToDestinationSurface).
VideoRect videoArea(int windowW, int windowH, int streamW, int streamH, int topInset);

// The same rectangle with y counted from the bottom of the window (Direct3D's
// screen space, see StreamUtils::screenSpaceToNormalizedDeviceCoords).
VideoRect toBottomUp(const VideoRect& rect, int windowH);

}
