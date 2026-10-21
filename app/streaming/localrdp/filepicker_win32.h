#pragma once

#include <QStringList>

#include <functional>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace LocalRdp {

// Shows the Windows "open files" dialog (several files allowed) on its own thread,
// so the stream keeps running while the user picks. `done` is called on that
// thread with the chosen paths (empty if cancelled).
void pickFilesAsync(HWND owner, const QString& title, std::function<void(QStringList)> done);

}
