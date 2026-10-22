#include "tests.h"
#include "screennudge_win32.h"

#include <QElapsedTimer>
#include <QTest>

#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

// Real screen capture, the way Sunshine does it: after the nudge, desktop
// duplication must deliver a frame that changed the monitor's corner.
// Opt-in (LOCALRDP_NUDGE_TEST=1): it looks at the real screen, and only says
// something when nothing else is moving on the first monitor (no video, no
// stream running) - otherwise it passes with or without the nudge.
void TestScreenNudge::captureGetsNewFrame()
{
    if (!qEnvironmentVariableIsSet("LOCALRDP_NUDGE_TEST")) {
        QSKIP("Set LOCALRDP_NUDGE_TEST=1 to run (uses real screen capture)");
    }
    ID3D11Device* device = nullptr;
    QVERIFY(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                        D3D11_SDK_VERSION, &device, nullptr, nullptr)));
    IDXGIDevice* dxgiDevice = nullptr;
    device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);
    IDXGIOutput* output = nullptr;
    QVERIFY(SUCCEEDED(adapter->EnumOutputs(0, &output)));
    IDXGIOutput1* output1 = nullptr;
    output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
    IDXGIOutputDuplication* dup = nullptr;
    QVERIFY(SUCCEEDED(output1->DuplicateOutput(device, &dup)));

    // Drain what is pending (the first frame is always delivered)
    // True if a new frame changed the monitor's top-left corner (where the nudge is);
    // other activity on the screen does not count
    auto acquire = [dup](UINT timeoutMs) {
        DXGI_OUTDUPL_FRAME_INFO info = {};
        IDXGIResource* res = nullptr;
        const HRESULT hr = dup->AcquireNextFrame(timeoutMs, &info, &res);
        bool corner = false;
        if (SUCCEEDED(hr)) {
            if (info.TotalMetadataBufferSize > 0) {
                std::vector<RECT> rects(info.TotalMetadataBufferSize / sizeof(RECT) + 1);
                UINT bytes = 0;
                if (SUCCEEDED(dup->GetFrameDirtyRects((UINT)(rects.size() * sizeof(RECT)), rects.data(), &bytes))) {
                    for (UINT i = 0; i < bytes / sizeof(RECT); i++) {
                        corner |= rects[i].left <= 0 && rects[i].top <= 0 && rects[i].right > 0 && rects[i].bottom > 0;
                    }
                }
            }
            res->Release();
            dup->ReleaseFrame();
        }
        return corner;
    };
    for (int i = 0; i < 20; i++) {
        acquire(30);
    }

    LocalRdp::nudgeScreens();
    bool gotFrame = false;
    QElapsedTimer t;
    t.start();
    while (!gotFrame && t.elapsed() < 1500) {
        QCoreApplication::processEvents();
        gotFrame = acquire(20);
    }
    QTest::qWait(200); // let the nudge window go away

    dup->Release();
    output1->Release();
    output->Release();
    adapter->Release();
    dxgiDevice->Release();
    device->Release();
    QVERIFY(gotFrame);
}
