#include "clipboardbridge_win32.h"
#include "clipreader_win32.h"
#include "remotedataobject_win32.h"

#include <ole2.h>

namespace LocalRdp {
namespace Clip {

using Proto::MsgType;

static const UINT kRunQueue = WM_APP + 1;
static const UINT kQuit = WM_APP + 2;
static const wchar_t* const kClassName = L"LocalRdpClipboardBridge";

ClipboardBridge::ClipboardBridge(SendFn send, bool useSystemClipboard)
    : m_Send(std::move(send)),
      m_UseSystemClipboard(useSystemClipboard)
{
    m_Fetcher = std::make_shared<RemoteFetcher>([this](MsgType type, QByteArray payload) {
        if (m_Connected) {
            m_Send(type, payload);
        }
    });
    m_Thread = std::thread([this]() { threadMain(); });
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_Ready.wait(lock, [this]() { return m_Started; });
}

ClipboardBridge::~ClipboardBridge()
{
    m_Connected = false;
    m_Fetcher->disconnected(); // wakes a paste that is waiting for data
    if (m_Hwnd != nullptr) {
        PostMessageW(m_Hwnd, kQuit, 0, 0);
    }
    m_Thread.join();
}

void ClipboardBridge::post(std::function<void()> work)
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Queue.push_back(std::move(work));
    }
    if (m_Hwnd != nullptr) {
        PostMessageW(m_Hwnd, kRunQueue, 0, 0);
    }
}

void ClipboardBridge::setEnabled(bool enabled)
{
    m_Enabled = enabled;
    if (!enabled) {
        m_Source.clear();
    }
    else if (m_Connected) {
        post([this]() { announceLocalClipboard(); });
    }
}

void ClipboardBridge::connectionUp()
{
    m_Connected = true;
    post([this]() { announceLocalClipboard(); });
}

void ClipboardBridge::connectionDown()
{
    m_Connected = false;
    m_Fetcher->disconnected();
    m_Source.clear();
    post([this]() { dropOurObject(); });
}

void ClipboardBridge::onFrame(const Proto::Frame& frame)
{
    switch (frame.type) {
    case MsgType::ClipOffer:
        if (m_Enabled) {
            if (auto offer = decodeOffer(frame.payload)) {
                post([this, o = *offer]() { acceptPeerOffer(o); });
            }
        }
        break;
    case MsgType::ClipRequest:
        if (auto request = decodeRequest(frame.payload)) {
            // Small reads (≤1 MiB) straight from the snapshot; never the live clipboard
            m_Source.serve(*request, m_Send);
        }
        break;
    case MsgType::ClipData:
        m_Fetcher->onData(frame.payload);
        break;
    case MsgType::ClipError:
        m_Fetcher->onError(frame.payload);
        break;
    default:
        break;
    }
}

void ClipboardBridge::offerFiles(const QStringList& paths)
{
    post([this, paths]() {
        if (!m_Enabled || !m_Connected) {
            return;
        }
        const LocalSnapshot s = snapshotFromPaths(paths, m_NextSeq++);
        if (!s.offer.files.isEmpty()) {
            sendSnapshot(s);
        }
    });
}

void ClipboardBridge::sendSnapshot(const LocalSnapshot& s)
{
    m_Source.set(s.offer, s.text, s.dib, s.png, s.filePaths);
    m_Send(MsgType::ClipOffer, encodeOffer(s.offer));
}

void ClipboardBridge::announceLocalClipboard()
{
    if (!m_UseSystemClipboard || !m_Enabled || !m_Connected) {
        return;
    }
    // Our own copy of the peer's data must not be bounced back
    if (m_OurObject != nullptr && OleIsCurrentClipboard(m_OurObject) == S_OK) {
        return;
    }
    IDataObject* data = nullptr;
    if (OleGetClipboard(&data) != S_OK || data == nullptr) {
        return;
    }
    const LocalSnapshot s = readDataObject(data, m_NextSeq++);
    data->Release();
    if (s.excluded || s.offer.isEmpty()) {
        return; // private (password) or nothing we can share
    }
    sendSnapshot(s);
}

void ClipboardBridge::setPeerOfferObserver(std::function<void(const Offer&)> observer)
{
    post([this, observer]() { m_PeerOfferObserver = observer; });
}

void ClipboardBridge::acceptPeerOffer(const Offer& offer)
{
    m_Fetcher->setOffer(offer);
    if (m_PeerOfferObserver) {
        m_PeerOfferObserver(offer);
    }
    if (!m_UseSystemClipboard || offer.isEmpty()) {
        return;
    }
    IDataObject* object = createRemoteDataObject(offer, m_Fetcher);
    if (OleSetClipboard(object) == S_OK) {
        dropOurObjectReference();
        m_OurObject = object; // the clipboard holds its own reference too
    }
    else {
        object->Release();
    }
}

void ClipboardBridge::dropOurObject()
{
    if (m_OurObject == nullptr) {
        return;
    }
    if (OleIsCurrentClipboard(m_OurObject) == S_OK) {
        // The peer's data can no longer be fetched: don't leave a dead paste behind
        OleSetClipboard(nullptr);
    }
    dropOurObjectReference();
}

void ClipboardBridge::dropOurObjectReference()
{
    if (m_OurObject != nullptr) {
        m_OurObject->Release();
        m_OurObject = nullptr;
    }
}

LRESULT CALLBACK ClipboardBridge::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto self = (ClipboardBridge*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self != nullptr) {
        if (msg == WM_CLIPBOARDUPDATE) {
            self->announceLocalClipboard();
            return 0;
        }
        if (msg == kRunQueue) {
            std::deque<std::function<void()>> work;
            {
                std::lock_guard<std::mutex> lock(self->m_Mutex);
                work.swap(self->m_Queue);
            }
            for (auto& w : work) {
                w();
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ClipboardBridge::threadMain()
{
    OleInitialize(nullptr);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &ClipboardBridge::windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    m_Hwnd = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, (LONG_PTR)this);
    if (m_UseSystemClipboard) {
        AddClipboardFormatListener(m_Hwnd);
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Started = true;
    }
    m_Ready.notify_all();
    // Work posted before the window existed
    PostMessageW(m_Hwnd, kRunQueue, 0, 0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == m_Hwnd && msg.message == kQuit) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    dropOurObject();
    if (m_UseSystemClipboard) {
        RemoveClipboardFormatListener(m_Hwnd);
    }
    SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, 0);
    DestroyWindow(m_Hwnd);
    OleUninitialize();
}

}
}
