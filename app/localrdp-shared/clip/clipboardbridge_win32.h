#pragma once

#include "clipexchange.h"
#include "clipreader_win32.h"
#include "localrdp-shared/protocol.h"

#include <QStringList>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>

namespace LocalRdp {
namespace Clip {

// Clipboard sync for one side. Runs its own thread (OLE STA with a hidden message
// window), so neither the stream loop nor the network thread ever waits on the
// clipboard. Local clipboard changes are announced to the peer (ClipOffer); the
// peer's offers are put on our clipboard as a lazy data object. File reads for the
// peer run on a separate I/O thread (OneDrive placeholders can take seconds).
class ClipboardBridge
{
public:
    // send: puts a frame on the connection; called from the bridge's threads.
    // useSystemClipboard = false: never touch the real clipboard (tests).
    explicit ClipboardBridge(SendFn send, bool useSystemClipboard = true);
    ~ClipboardBridge();

    ClipboardBridge(const ClipboardBridge&) = delete;
    ClipboardBridge& operator=(const ClipboardBridge&) = delete;

    void setEnabled(bool enabled);
    void connectionUp();   // announce the current clipboard
    void connectionDown(); // forget both sides' offers

    // ClipOffer / ClipRequest / ClipData / ClipError from the network thread.
    void onFrame(const Proto::Frame& frame);

    // "Send files to PC" and drag and drop: offer these as if they were copied.
    // done(ok, reason) is called on the bridge thread with the outcome.
    void offerFiles(const QStringList& paths, std::function<void(bool, QString)> done = {});

    // Called (on the clipboard thread) for every offer accepted from the peer: logs, tests.
    void setPeerOfferObserver(std::function<void(const Offer&)> observer);

private:
    void threadMain();
    void ioThreadMain();
    void post(std::function<void()> work);
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Clipboard thread only
    void announceLocalClipboard();
    void acceptPeerOffer(const Offer& offer);
    void onRetryTimer();
    void dropOurObject();
    void dropOurObjectReference();
    void sendSnapshot(const LocalSnapshot& snapshot);

    SendFn m_Send;
    const bool m_UseSystemClipboard;
    std::atomic<bool> m_Enabled { true };
    std::atomic<bool> m_Connected { false };
    std::function<void(const Offer&)> m_PeerOfferObserver; // clipboard thread

    LocalClipSource m_Source;
    std::shared_ptr<RemoteFetcher> m_Fetcher;
    quint64 m_NextSeq;                  // clipboard thread; random start (see constructor)
    IDataObject* m_OurObject = nullptr; // clipboard thread: the peer's data on our clipboard

    // Retries while another program holds the clipboard (clipboard thread)
    int m_AnnounceRetries = 0;
    bool m_AnnouncePending = false;
    std::optional<Offer> m_PendingPeerOffer;
    int m_PeerOfferRetries = 0;

    std::thread m_Thread;
    HWND m_Hwnd = nullptr;
    std::mutex m_Mutex;
    std::condition_variable m_Ready;
    std::deque<std::function<void()>> m_Queue;
    bool m_Started = false;

    // File requests from the peer, served off the network thread
    std::thread m_IoThread;
    std::mutex m_IoMutex;
    std::condition_variable m_IoCv;
    std::deque<Request> m_IoQueue;
    bool m_IoStop = false;
};

}
}
