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
// peer's offers are put on our clipboard as a lazy data object.
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
    void offerFiles(const QStringList& paths);

private:
    void threadMain();
    void post(std::function<void()> work);
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Clipboard thread only
    void announceLocalClipboard();
    void acceptPeerOffer(const Offer& offer);
    void dropOurObject();
    void dropOurObjectReference();
    void sendSnapshot(const LocalSnapshot& snapshot);

    SendFn m_Send;
    const bool m_UseSystemClipboard;
    std::atomic<bool> m_Enabled { true };
    std::atomic<bool> m_Connected { false };

    LocalClipSource m_Source;
    std::shared_ptr<RemoteFetcher> m_Fetcher;
    quint64 m_NextSeq = 1;          // clipboard thread
    IDataObject* m_OurObject = nullptr; // clipboard thread: the peer's data on our clipboard

    std::thread m_Thread;
    HWND m_Hwnd = nullptr;
    std::mutex m_Mutex;
    std::condition_variable m_Ready;
    std::deque<std::function<void()>> m_Queue;
    bool m_Started = false;
};

}
}
