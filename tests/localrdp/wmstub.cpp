// Test-only stand-ins for the window-manager probes used by StreamingPreferences.
// The real implementations (app/wm.cpp) need SDL and a display.
#include "utils.h"

namespace WMUtils {
bool isRunningX11() { return false; }
bool isRunningNvidiaProprietaryDriverX11() { return false; }
bool supportsDesktopGLWithEGL() { return false; }
bool isRunningWayland() { return false; }
bool isRunningWindowManager() { return true; }
bool isRunningDesktopEnvironment() { return true; }
QString getDrmCardOverride() { return QString(); }
bool isGpuSlow() { return false; }
}
