// Atrium's window switcher (Alt + Tab, shell/alt_tab.h): a piece of glass in the middle of the screen with a card
// per window, most recently used first: its live picture (a DWM thumbnail), its app's icon and its title. The
// selection moves with Tab and the arrows; letting go of Alt (Commit) brings the selected window to the front.
// A click on a card picks it. It pops open and closes as every popup does (Surface).
#pragma once

#include "motion.h"
#include "ui/surface.h"

#include <dwmapi.h>

#include <functional>
#include <string>
#include <vector>

namespace atrium {

class WindowSwitcher {
public:
    struct Entry {
        HWND window = nullptr;
        std::wstring key; // its app (the icon cache's key)
        IconImage icon;
    };

    explicit WindowSwitcher(HINSTANCE instance);
    ~WindowSwitcher();
    WindowSwitcher(const WindowSwitcher&) = delete;
    WindowSwitcher& operator=(const WindowSwitcher&) = delete;

    bool Visible() const { return surface_.Visible(); }
    // Opens with `entries` (most recently used first; the second is selected, or the last when `back`).
    // False, and nothing shown, with fewer than two windows.
    bool Open(std::vector<Entry> entries, bool back);
    void Step(bool back);
    void Commit(); // the selected window comes to the front
    void Cancel();

    std::function<void(HWND)> onActivate;

private:
    struct Card {
        Entry entry;
        std::wstring title;
        HTHUMBNAIL thumb = nullptr;
        RECT sourceRect{};
        D2D1_RECT_F rect{}, image{};
        motion::Follow chosen;
    };

    void Paint(Canvas& c);
    bool Message(UINT m, WPARAM w, LPARAM l, LRESULT& result);
    void Layout(float maxWidthDip);
    void UpdateThumbs();
    void Close();

    Surface surface_;
    std::vector<Card> cards_;
    D2D1_SIZE_F size_{};
    int selected_ = 0;
    bool ready_ = false; // the glass has popped out: the pictures show
};

} // namespace atrium
