#pragma once

// SDL_USEREVENT codes used by the LocalRDP in-stream menu (other codes live in
// session.cpp and video/decoder.h). Menu work that touches the stream always runs
// from the session loop, never from inside a window procedure, which runs within
// SDL's own event pump.

// Open/close the menu (hotkey or button click).
#define SDL_CODE_TOGGLE_STREAM_MENU 106
// A menu item was chosen; data1 = LocalRdp::StreamAction.
#define SDL_CODE_STREAM_MENU_ACTION 107
// The menu closed itself (click outside, capture lost).
#define SDL_CODE_STREAM_MENU_DISMISSED 108
