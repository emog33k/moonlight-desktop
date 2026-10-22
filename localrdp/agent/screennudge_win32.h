#pragma once

namespace LocalRdp {

// Screen capture (Sunshine's DXGI desktop duplication) only delivers a frame
// when something on the monitor changes; after switching to another monitor
// the stream shows nothing new until then. This makes every monitor change
// for a moment without anything visible: a 2x2 almost transparent window
// flashes in its top-left corner. Call on a thread with a message loop.
void nudgeScreens();

}
