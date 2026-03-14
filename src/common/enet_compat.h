// Windows-safe enet include.
// On Windows, enet pulls in windows.h which clashes with raylib.
// raylib.h MUST be included before windows.h to avoid mangled names
// (LoadImage->LoadImageA, DrawText->DrawTextA, etc.).
// This header ensures the correct include order and suppresses the
// remaining conflicts (Rectangle, CloseWindow, ShowCursor).
#pragma once

#include "raylib.h"

#if defined(_WIN32)
    #define NOGDI                              // prevents wingdi.h Rectangle()
    #define CloseWindow _win32_CloseWindow     // rename away winuser.h version
    #define ShowCursor  _win32_ShowCursor      // rename away winuser.h version
#endif

#include <enet/enet.h>

#if defined(_WIN32)
    #undef NOGDI
    #undef CloseWindow
    #undef ShowCursor
    #undef DrawText
    #undef LoadImage
#endif
