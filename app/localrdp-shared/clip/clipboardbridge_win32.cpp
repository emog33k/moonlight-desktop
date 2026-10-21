#include "clipboardbridge_win32.h"
#include "remotedataobject_win32.h"

#include <QRandomGenerator>

#include <ole2.h>

#include <mutex>

namespace LocalRdp {
namespace Clip {

using Proto::MsgType;

static const UINT kRunQueue = WM_APP + 1;
static const UINT kQuit = WM_APP + 2;
static const UINT_PTR kRetryTimer = 1;
static const UINT kRetryDelayMs = 40;
static const int kMaxRetries = 5;
static const wchar_t* const kClassName = L"LocalRdpClipboardBridge";

ClipboardBridge::ClipboardBridge(SendFn send, bool useSystemClipboard)
    : m_Send(std::move(send)),
      m_UseSystemClipboard(useSystemClipboard)
{
    // Random start: after a restart, a paste still running from an old data object
    // can never match a fresh offer by accident. Stays well below 2^53 (JSON-safe).
    m_NextSeq = QRandomGenerator::global()->bounded((quint64)1 << 40) + 1000000;

    m_Fetcher = std::make_shared<RemoteFetcher>([this](MsgType type, QByteArray payload) {
        if (m_Connected) {
            m_Send(type, payload);
        }
    });
    m_IoThread = std::thread([this]() { ioThreadMain(); });
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
    {
        std::lock_guard<std::mutex> lock(m_IoMutex);
        m_IoStop = true;
    }
    m_IoCv.notify_all();
    m_IoThread.join();
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
        post([this]() { dropOurObject(); });
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
                post([this, o = *offer]() {
                    m_PendingPeerOffer.reset(); // a newer offer replaces one still retrying
                    m_PeerOfferRetries = 0;
                    acceptPeerOffer(o);
                });
            }
        }
        break;
    case MsgType::ClipRequest:
        if (auto request = decodeRequest(frame.payload)) {
            if (request->what == What::File) {
                // Disk can be slow (OneDrive placeholders): never on the network thread
                {
                    std::lock_guard<std::mutex> lock(m_IoMutex);
                    m_IoQueue.push_back(*request);
                }
                m_IoCv.notify_one();
            }
            else {
                m_Source.serve(*request, m_Send); // already in memory
            }
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

void ClipboardBridge::ioThreadMain()
{
    for (;;) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(m_IoMutex);
            m_IoCv.wait(lock, [this]() { return m_IoStop || !m_IoQueue.empty(); });
            if (m_IoStop) {
                return;
            }
            request = m_IoQueue.front();
            m_IoQueue.pop_front();
        }
        m_Source.serve(request, m_Send);
    }
}

void ClipboardBridge::offerFiles(const QStringList& paths, std::function<void(bool, QString)> done)
{
    post([this, paths, done]() {
        auto report = [&done](bool ok, const QString& reason) {
            if (done) {
                done(ok, reason);
            }
        };
        if (!m_Enabled) {
            report(false, QStringLiteral("синхронизация буфера выключена"));
            return;
        }
        if (!m_Connected) {
            report(false, QStringLiteral("нет связи с агентом на ПК"));
            return;
        }
        const LocalSnapshot s = snapshotFromPaths(paths, m_NextSeq++);
        if (s.offer.files.isEmpty()) {
            report(false, s.filesProblem.isEmpty() ? QStringLiteral("нечего отправлять") : s.filesProblem);
            return;
        }
        sendSnapshot(s);
        report(true, QString());
    });
}

void ClipboardBridge::setPeerOfferObserver(std::function<void(const Offer&)> observer)
{
    post([this, observer]() { m_PeerOfferObserver = observer; });
}

void ClipboardBridge::sendSnapshot(const LocalSnapshot& s)
{
    m_Source.set(s.offer, s.text, s.dib, s.png, s.filePaths);
    m_Send(MsgType::ClipOffer, encodeOffer(s.offer));
}

void ClipboardBridge::announceLocalClipboard()
{
    m_AnnouncePending = false;
    if (!m_UseSystemClipboard || !m_Enabled || !m_Connected) {
        return;
    }
    // Our own copy of the peer's data must not be bounced back
    if (m_OurObject != nullptr && OleIsCurrentClipboard(m_OurObject) == S_OK) {
        return;
    }
    IDataObject* data = nullptr;
    const HRESULT hr = OleGetClipboard(&data);
    LocalSnapshot s;
    if (hr == S_OK && data != nullptr) {
        s = readDataObject(data, m_NextSeq++);
        data->Release();
    }
    else {
        s.busy = (hr == CLIPBRD_E_CANT_OPEN);
    }

    if (s.busy) {
        // Clipboard history / managers open the clipboard on every change: try again shortly
        if (m_AnnounceRetries++ < kMaxRetries) {
            m_AnnouncePending = true;
            SetTimer(m_Hwnd, kRetryTimer, kRetryDelayMs, nullptr);
            return;
        }
    }
    m_AnnounceRetries = 0;

    if (s.excluded) {
        return; // private (password manager): the peer keeps what it had
    }
    // Also when nothing can be shared (only unsupported formats, too many files):
    // an empty offer makes the peer drop the previous, now outdated, content.
    sendSnapshot(s);
}

void ClipboardBridge::acceptPeerOffer(const Offer& offer)
{
    if (m_PeerOfferObserver && m_PeerOfferRetries == 0) {
        m_PeerOfferObserver(offer);
    }
    if (offer.isEmpty() || !m_UseSystemClipboard) {
        m_Fetcher->setOffer(offer); // old pastes stop getting data
        dropOurObject();
        return;
    }

    IDataObject* object = createRemoteDataObject(offer, m_Fetcher);
    const HRESULT hr = OleSetClipboard(object);
    if (hr == S_OK) {
        // Only now: until the new object is on the clipboard, the old one stays valid
        m_Fetcher->setOffer(offer);
        dropOurObjectReference();
        m_OurObject = object; // the clipboard holds its own reference too
        m_PeerOfferRetries = 0;
        return;
    }
    object->Release();

    if (hr == CLIPBRD_E_CANT_OPEN && m_PeerOfferRetries++ < kMaxRetries) {
        m_PendingPeerOffer = offer;
        SetTimer(m_Hwnd, kRetryTimer, kRetryDelayMs, nullptr);
        return;
    }
    // Gave up: never leave the outdated content pasteable
    m_PeerOfferRetries = 0;
    m_Fetcher->setOffer(offer);
    dropOurObject();
}

void ClipboardBridge::onRetryTimer()
{
    KillTimer(m_Hwnd, kRetryTimer);
    if (m_PendingPeerOffer) {
        const Offer offer = *m_PendingPeerOffer;
        m_PendingPeerOffer.reset();
        acceptPeerOffer(offer);
    }
    if (m_AnnouncePending) {
        announceLocalClipboard();
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
        switch (msg) {
        case WM_CLIPBOARDUPDATE:
            self->m_AnnounceRetries = 0;
            self->announceLocalClipboard();
            return 0;
        case WM_TIMER:
            if (wParam == kRetryTimer) {
                self->onRetryTimer();
                return 0;
            }
            break;
        case kRunQueue:
        {
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
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ClipboardBridge::threadMain()
{
    OleInitialize(nullptr);

    static std::once_flag registered;
    std::call_once(registered, []() {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &ClipboardBridge::windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
    });
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

    KillTimer(m_Hwnd, kRetryTimer);
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
