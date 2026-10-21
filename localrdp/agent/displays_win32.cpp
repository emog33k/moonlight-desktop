#include "displays_win32.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>

namespace LocalRdp {

using namespace Proto;

QVector<HostDisplay> enumerateDisplays()
{
    QVector<HostDisplay> displays;

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) || factory == nullptr) {
        return displays;
    }

    IDXGIAdapter1* adapter = nullptr;
    for (UINT a = 0; SUCCEEDED(factory->EnumAdapters1(a, &adapter)); a++) {
        IDXGIOutput* output = nullptr;
        for (UINT o = 0; SUCCEEDED(adapter->EnumOutputs(o, &output)); o++) {
            DXGI_OUTPUT_DESC desc;
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.AttachedToDesktop) {
                HostDisplay d;
                d.name = QString::fromWCharArray(desc.DeviceName);
                d.width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
                d.height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
                MONITORINFO mi = { sizeof(mi) };
                d.primary = GetMonitorInfoW(desc.Monitor, &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY);
                if (displays.size() < kMaxDisplays && d.width > 0 && d.height > 0) {
                    displays << d;
                }
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    return displays;
}

}
