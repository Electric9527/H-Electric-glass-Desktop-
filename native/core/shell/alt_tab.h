// Alt + Tab opens Atrium's own window switcher (setting glassSwitcher) instead of Windows'. A low-level keyboard
// hook on a thread of its own asks the UI thread, each time Tab goes down with Alt held, to open the switcher or
// move its selection; only when the UI thread answers in time that it did is the key kept from Windows. So a busy
// or failed switcher, an elevated window in front, or fewer than two windows leave Alt + Tab to Windows as before.
// While the switcher is open: Tab / Shift+Tab and the arrow keys move, Esc cancels, letting go of Alt switches.
#pragma once

#include <windows.h>

#include <atomic>
#include <thread>

namespace atrium {

class AltTab {
public:
    enum : WPARAM { kCommit = 0, kNext = 1, kPrevious = 2, kCancel = 3 };
    // Sends `msg` to `notify` (wParam above); the window procedure returns 1 when the switcher took kNext/kPrevious.
    AltTab(HWND notify, UINT msg);
    ~AltTab();
    AltTab(const AltTab&) = delete;
    AltTab& operator=(const AltTab&) = delete;

private:
    static LRESULT CALLBACK Hook(int code, WPARAM w, LPARAM l);
    void Run();
    bool Ask(WPARAM what) const;
    HWND notify_;
    UINT msg_;
    DWORD threadId_ = 0;
    std::atomic<bool> ready_{false};
    bool open_ = false; // the switcher is up: its keys are ours until Alt comes up
    std::thread thread_;
};

} // namespace atrium
