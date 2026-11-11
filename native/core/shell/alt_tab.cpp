#include "shell/alt_tab.h"

#include "log.h"

namespace atrium {

namespace {

AltTab* g_altTab = nullptr; // the hook procedure has no context of its own

// The window in front runs elevated: Atrium could not bring another window forward over it reliably.
bool ForegroundElevated()
{
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (!fg || !GetWindowThreadProcessId(fg, &pid) || !pid)
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return true;
    bool elevated = true;
    HANDLE token = nullptr;
    if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION e{};
        DWORD size = 0;
        elevated = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size) && e.TokenIsElevated;
        CloseHandle(token);
    }
    CloseHandle(process);
    return elevated;
}

} // namespace

AltTab::AltTab(HWND notify, UINT msg) : notify_(notify), msg_(msg)
{
    g_altTab = this;
    thread_ = std::thread([this] { Run(); });
    while (!ready_.load())
        Sleep(1);
}

AltTab::~AltTab()
{
    if (threadId_)
        PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    thread_.join();
    g_altTab = nullptr;
}

void AltTab::Run()
{
    threadId_ = GetCurrentThreadId();
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, Hook, GetModuleHandleW(nullptr), 0);
    Log(hook ? L"alttab: watching Alt + Tab (the glass switcher)" : L"alttab: could not watch the keyboard");
    ready_ = true;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        DispatchMessageW(&msg);
    if (hook)
        UnhookWindowsHookEx(hook);
}

// The UI thread took it (in 150 ms: otherwise the key goes to Windows, and the switcher is told to close).
bool AltTab::Ask(WPARAM what) const
{
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(notify_, msg_, what, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 150, &result))
        return result == 1;
    PostMessageW(notify_, msg_, kCancel, 0);
    return false;
}

LRESULT CALLBACK AltTab::Hook(int code, WPARAM w, LPARAM l)
{
    AltTab* self = g_altTab;
    const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l);
    if (code != HC_ACTION || !self || (k->flags & LLKHF_INJECTED))
        return CallNextHookEx(nullptr, code, w, l);
    const bool down = w == WM_KEYDOWN || w == WM_SYSKEYDOWN;
    const DWORD vk = k->vkCode;
    const bool alt = vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU;
    if (self->open_) {
        if (alt && !down) { // Alt let go: switch. The key itself goes on, so no app keeps Alt down.
            self->open_ = false;
            PostMessageW(self->notify_, self->msg_, kCommit, 0);
            return CallNextHookEx(nullptr, code, w, l);
        }
        if (vk == VK_TAB || vk == VK_RIGHT || vk == VK_LEFT || vk == VK_DOWN || vk == VK_UP) {
            if (down) {
                const bool back = vk == VK_LEFT || vk == VK_UP || (vk == VK_TAB && (GetAsyncKeyState(VK_SHIFT) & 0x8000));
                PostMessageW(self->notify_, self->msg_, back ? kPrevious : kNext, 0);
            }
            return 1;
        }
        if (vk == VK_ESCAPE) {
            if (down) {
                self->open_ = false;
                PostMessageW(self->notify_, self->msg_, kCancel, 0);
            }
            return 1;
        }
        return CallNextHookEx(nullptr, code, w, l);
    }
    if (vk == VK_TAB && down && (k->flags & LLKHF_ALTDOWN) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
        !((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) && !ForegroundElevated()) {
        if (self->Ask((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? kPrevious : kNext)) {
            self->open_ = true;
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}

} // namespace atrium
