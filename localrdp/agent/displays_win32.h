#pragma once

#include "localrdp-shared/hostdisplays.h"

namespace LocalRdp {

// Monitors in the order Sunshine numbers them for Ctrl+Alt+Shift+F1..F12:
// DXGI adapters, then each adapter's outputs that are part of the desktop
// (Sunshine's display_names()). Sunshine additionally drops outputs it cannot
// capture, which only happens on unusual setups (e.g. some hybrid-GPU laptops).
QVector<Proto::HostDisplay> enumerateDisplays();

}
