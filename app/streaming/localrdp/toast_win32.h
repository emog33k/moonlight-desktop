#pragma once

#include <QRect>
#include <QString>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace LocalRdp {

// A short notice ("Файлы в буфере ПК…") shown near the floating button for a few
// seconds. Never takes focus or clicks; drawn with Segoe UI so any text works.
class ToastWindow
{
public:
    explicit ToastWindow(HWND owner);
    ~ToastWindow();

    ToastWindow(const ToastWindow&) = delete;
    ToastWindow& operator=(const ToastWindow&) = delete;

    // Shown inside `area` (the stream window), next to `anchor` (the button).
    void show(const QString& text, const QRect& anchor, const QRect& area, int durationMs);
    void hide();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_Owner;
    HWND m_Hwnd = nullptr;
};

}
