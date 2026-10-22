#include "clipboardbridge_win32.h"
#include "remotedataobject_win32.h"

#include <QDateTime>
#include <QRandomGenerator>

#include <ole2.h>

#include <mutex>

namespace LocalRdp {
namespace Clip {

using Proto::MsgType;

static const UINT kRunQueue = WM_APP + 1;
static const UINT kQuit = WM_APP + 2;
static const UINT_PTR kRetryTimer = 1;
static const UINT_PTR kHeartbeatTimer = 2;
static const UINT kRetryDelayMs = 40;
static const UINT kHeartbeatMs = 3000;
static const int kMaxRetries = 5;
static const size_t kMaxQueuedFileRequests = 64;
static const wchar_t* const kClassName = L"LocalRdpClipboardBridge";

static qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

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
    else {
        PostThreadMessageW(m_ThreadId, WM_QUIT, 0, 0);
    }
    m_Thread.join();
    {
        std::lock_guard<std::mutex> lock(m_IoMutex);
        m_IoStop = true;
    }
    m_IoCv.notify_all();
    // A read of a slow file (e.g. a OneDrive file downloading) must not hold up
    // the end of the stream: cancel until the thread is out (it may only be
    // about to start the read when we first try)
    const HANDLE io = (HANDLE)m_IoThread.native_handle();
    while (WaitForSingleObject(io, 50) == WAIT_TIMEOUT) {
        CancelSynchronousIo(io);
    }
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

void ClipboardBridge::setLocalCopyTime(qint64 msSinceEpoch)
{
    m_LocalCopiedAt = msSinceEpoch;
}

void ClipboardBridge::stopServing()
{
    m_Epoch++;
    m_Source.clear();
    {
        std::lock_guard<std::mutex> lock(m_IoMutex);
        m_IoQueue.clear();
    }
}

void ClipboardBridge::setEnabled(bool enabled)
{
    m_Enabled = enabled;
    if (!enabled) {
        // At once, from this thread: the clipboard thread may be busy for a while
        // (e.g. waiting on a slow paste), and the PC may have just been locked
        stopServing();
        m_Fetcher->disconnected(); // also ends pastes of the peer's data in progress
    }
    post([this, enabled]() {
        if (m_Enabled != enabled) {
            return; // changed again meanwhile; that task does the work
        }
        if (!enabled) {
            m_Source.clear();
            m_PendingPeerOffer.reset();
            m_AnnouncePending = false;
            dropOurObject();
            if (m_Connected) {
                // The peer drops what we had offered
                Offer nothing;
                nothing.seq = m_NextSeq++;
                m_Send(MsgType::ClipOffer, encodeOffer(nothing));
            }
        }
        else if (m_Connected) {
            m_AnnounceInitial = true;
            m_AnnounceRetries = 0;
            announceLocalClipboard();
        }
    });
}

void ClipboardBridge::connectionUp()
{
    m_Connected = true;
    post([this]() {
        m_AnnounceInitial = true;
        m_AnnounceRetries = 0;
        announceLocalClipboard();
    });
}

void ClipboardBridge::connectionDown()
{
    m_Connected = false;
    m_Fetcher->disconnected();
    stopServing();
    post([this]() {
        m_Source.clear();
        m_PendingPeerOffer.reset();
        m_AnnouncePending = false;
        dropOurObject();
    });
}

void ClipboardBridge::onFrame(const Proto::Frame& frame)
{
    switch (frame.type) {
    case MsgType::ClipOffer:
        if (m_Enabled && m_Connected) {
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
            if (!m_Enabled || !m_Connected) {
                m_Send(MsgType::ClipError, encodeError(request->id, "off"));
            }
            else if (request->what == What::File) {
                // Disk can be slow (OneDrive placeholders): never on the network thread
                bool queued = false;
                {
                    std::lock_guard<std::mutex> lock(m_IoMutex);
                    if (m_IoQueue.size() < kMaxQueuedFileRequests) {
                        m_IoQueue.push_back({ *request, m_Epoch.load() });
                        queued = true;
                    }
                }
                if (queued) {
                    m_IoCv.notify_one();
                }
                else {
                    m_Send(MsgType::ClipError, encodeError(request->id, "busy"));
                }
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
        quint64 epoch;
        {
            std::unique_lock<std::mutex> lock(m_IoMutex);
            m_IoCv.wait(lock, [this]() { return m_IoStop || !m_IoQueue.empty(); });
            if (m_IoStop) {
                return;
            }
            request = m_IoQueue.front().first;
            epoch = m_IoQueue.front().second;
            m_IoQueue.pop_front();
        }
        if (epoch != m_Epoch || !m_Enabled) {
            continue; // from a connection that is gone, or sharing was turned off
        }
        // While this read runs, the clipboard thread tells the peer we are still
        // at it (see onHeartbeatTimer), so a slow disk does not look like a dead peer.
        m_IoBusySince = nowMs();
        m_IoBusyEpoch = epoch;
        m_IoBusyId = request.id;
        m_Source.serve(request, [this, epoch](MsgType type, QByteArray payload) {
            // The read may have taken long: only to the connection that asked
            if (epoch == m_Epoch && m_Enabled) {
                m_Send(type, payload);
            }
        });
        m_IoBusyId = 0;
    }
}

void ClipboardBridge::onHeartbeatTimer()
{
    const quint32 id = m_IoBusyId;
    if (id != 0 && m_Connected && m_Enabled && m_IoBusyEpoch == m_Epoch &&
            nowMs() - m_IoBusySince >= (qint64)kHeartbeatMs) {
        m_Send(MsgType::ClipData, encodeChunk({ id, false, QByteArray() }));
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
        m_LocalCopiedAt = nowMs();
        sendSnapshot(s, false);
        report(true, QString());
    });
}

void ClipboardBridge::setPeerOfferObserver(std::function<void(const Offer&)> observer)
{
    post([this, observer]() { m_PeerOfferObserver = observer; });
}

void ClipboardBridge::sendSnapshot(const LocalSnapshot& s, bool initial)
{
    if (!m_Enabled || !m_Connected) {
        return; // turned off or disconnected while reading the clipboard
    }
    Offer offer = s.offer;
    offer.copiedAt = m_LocalCopiedAt;
    offer.initial = initial;
    m_Source.set(offer, s.text, s.dib, s.png, s.filePaths);
    m_Send(MsgType::ClipOffer, encodeOffer(offer));
}

void ClipboardBridge::announceLocalClipboard()
{
    m_AnnouncePending = false;
    if (!m_UseSystemClipboard || !m_Enabled || !m_Connected) {
        return;
    }
    // Our own copy of the peer's data must not be bounced back
    if (m_OurObject != nullptr && OleIsCurrentClipboard(m_OurObject) == S_OK) {
        m_AnnounceInitial = false;
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
    const bool initial = m_AnnounceInitial;
    m_AnnounceInitial = false;

    if (s.excluded) {
        return; // private (password manager): the peer keeps what it had
    }
    m_LocalHasContent = !s.offer.isEmpty();
    // Also when nothing can be shared (only unsupported formats, too many files):
    // an empty offer makes the peer drop the previous, now outdated, content.
    sendSnapshot(s, initial);
}

void ClipboardBridge::acceptPeerOffer(const Offer& offer)
{
    if (!m_Enabled || !m_Connected) {
        return; // a late offer after sync was turned off or the connection closed
    }
    if (offer.initial && m_PeerOfferRetries == 0) {
        // Connection start: both sides announce what they have; keep the newer one
        if (offer.isEmpty()) {
            return;
        }
        const qint64 local = m_LocalCopiedAt;
        if (m_LocalHasContent && (offer.copiedAt < local || (offer.copiedAt == local && !m_PreferPeerOnTie))) {
            return;
        }
    }
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
        m_LocalCopiedAt = offer.copiedAt > 0 ? offer.copiedAt : nowMs();
        m_LocalHasContent = true;
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
    if (m_DropPending) {
        dropOurObject();
    }
    if (m_AnnouncePending) {
        announceLocalClipboard();
    }
}

void ClipboardBridge::dropOurObject()
{
    m_DropPending = false;
    if (m_OurObject == nullptr) {
        return;
    }
    if (OleIsCurrentClipboard(m_OurObject) == S_OK) {
        // The peer's data can no longer be fetched: don't leave a dead paste behind
        if (OleSetClipboard(nullptr) == CLIPBRD_E_CANT_OPEN && m_DropRetries++ < kMaxRetries && m_Hwnd != nullptr) {
            m_DropPending = true; // someone holds the clipboard: try again shortly
            SetTimer(m_Hwnd, kRetryTimer, kRetryDelayMs, nullptr);
            return;
        }
    }
    m_DropRetries = 0;
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
    if (msg == kQuit) {
        // Also when dispatched from a nested message loop (e.g. a COM call)
        PostQuitMessage(0);
        return 0;
    }
    if (self != nullptr) {
        switch (msg) {
        case WM_CLIPBOARDUPDATE:
            // Someone copied on this computer (our own object is filtered out there)
            if (self->m_OurObject == nullptr || OleIsCurrentClipboard(self->m_OurObject) != S_OK) {
                self->m_LocalCopiedAt = nowMs();
            }
            self->m_AnnounceRetries = 0;
            self->m_AnnounceInitial = false;
            self->announceLocalClipboard();
            return 0;
        case WM_TIMER:
            if (wParam == kRetryTimer) {
                self->onRetryTimer();
                return 0;
            }
            if (wParam == kHeartbeatTimer) {
                self->onHeartbeatTimer();
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
    m_ThreadId = GetCurrentThreadId();

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
    if (m_Hwnd != nullptr) {
        SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, (LONG_PTR)this);
        if (m_UseSystemClipboard) {
            AddClipboardFormatListener(m_Hwnd);
        }
        SetTimer(m_Hwnd, kHeartbeatTimer, kHeartbeatMs, nullptr);
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Started = true;
    }
    m_Ready.notify_all();
    if (m_Hwnd != nullptr) {
        // Work posted before the window existed
        PostMessageW(m_Hwnd, kRunQueue, 0, 0);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == m_Hwnd && msg.message == kQuit) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (m_Hwnd != nullptr) {
        KillTimer(m_Hwnd, kRetryTimer);
        KillTimer(m_Hwnd, kHeartbeatTimer);
        m_DropRetries = kMaxRetries; // last chance, no retry timer any more
        dropOurObject();
        dropOurObjectReference();
        if (m_UseSystemClipboard) {
            RemoveClipboardFormatListener(m_Hwnd);
        }
        SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_Hwnd);
    }
    OleUninitialize();
}

}
}
