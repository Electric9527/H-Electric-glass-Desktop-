#include "shell/voice_input.h"

#include "log.h"

#include <dwmapi.h>
#include <objbase.h>
#include <uiautomation.h>

#include <atomic>
#include <cwctype>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace atrium {
namespace {

// A COM pointer, released when it goes.
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com()
    {
        if (p)
            p->Release();
    }
    T** put()
    {
        if (p)
            p->Release(), p = nullptr;
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::wstring Lower(std::wstring s)
{
    for (wchar_t& c : s)
        c = wchar_t(std::towlower(c));
    return s;
}

std::wstring ClassOf(HWND w)
{
    wchar_t name[256]{};
    GetClassNameW(w, name, 256);
    return name;
}

std::wstring TitleOf(HWND w)
{
    wchar_t title[256]{};
    GetWindowTextW(w, title, 256);
    return title;
}

std::wstring ProcessPathOf(HWND w)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    std::wstring path;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buffer[MAX_PATH * 2]{};
        DWORD size = DWORD(std::size(buffer));
        if (QueryFullProcessImageNameW(process, 0, buffer, &size))
            path = buffer;
        CloseHandle(process);
    }
    return path;
}

std::wstring FileName(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// A window of the input method: its program sits under ...\Tencent\WeType\, or its class says so.
bool IsWeType(HWND w)
{
    const std::wstring cls = Lower(ClassOf(w));
    if (cls.find(L"wetype") != std::wstring::npos)
        return true;
    const std::wstring path = Lower(ProcessPathOf(w));
    return path.find(L"\\wetype\\") != std::wstring::npos || FileName(path).rfind(L"wetype", 0) == 0;
}

bool Cloaked(HWND w)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
}

bool Ours(HWND w)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

// The app window the user was most likely typing in: the topmost visible, ordinary window of another process.
HWND TopAppWindow()
{
    for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (!IsWindowVisible(w) || IsIconic(w) || Cloaked(w) || Ours(w))
            continue;
        const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
        if ((ex & WS_EX_TOOLWINDOW) || (ex & WS_EX_NOACTIVATE) || GetWindow(w, GW_OWNER))
            continue;
        RECT r{};
        GetWindowRect(w, &r);
        if (r.right - r.left < 120 || r.bottom - r.top < 80 || IsWeType(w))
            continue;
        const std::wstring cls = ClassOf(w);
        if (cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd")
            continue;
        return w;
    }
    return nullptr;
}

std::wstring Describe(HWND w)
{
    if (!w)
        return L"(none)";
    RECT r{};
    GetWindowRect(w, &r);
    wchar_t text[700];
    swprintf(text, 700, L"%p class=%ls title=\"%ls\" exe=%ls rect=%ld,%ld,%ld,%ld%ls", static_cast<void*>(w), ClassOf(w).c_str(),
             TitleOf(w).substr(0, 60).c_str(), FileName(ProcessPathOf(w)).c_str(), r.left, r.top, r.right, r.bottom,
             IsWindowVisible(w) ? L"" : L" hidden");
    return text;
}

// The keyboard goes back to the app the user was in when Atrium itself has it (a menu or panel was just used).
void GiveKeyboardBack(HWND lastApp)
{
    HWND fg = GetForegroundWindow();
    Log(L"voice: foreground %ls", Describe(fg).c_str());
    if (fg && !Ours(fg))
        return;
    HWND app = lastApp && IsWindow(lastApp) && !Ours(lastApp) ? lastApp : TopAppWindow();
    if (!app) {
        Log(L"voice: no app window to give the keyboard back to");
        return;
    }
    const bool ok = SetForegroundWindow(app) != FALSE;
    Log(L"voice: keyboard back to %ls: %ls", Describe(app).c_str(), ok ? L"ok" : L"refused");
    Sleep(60);
}

struct WeTypeWindow {
    HWND hwnd;
    RECT rect;
};

std::vector<WeTypeWindow> VisibleWeTypeWindows()
{
    std::vector<WeTypeWindow> found;
    EnumWindows(
        [](HWND w, LPARAM out) -> BOOL {
            if (IsWindowVisible(w) && !Cloaked(w) && IsWeType(w)) {
                RECT r{};
                GetWindowRect(w, &r);
                if (r.right > r.left && r.bottom > r.top)
                    reinterpret_cast<std::vector<WeTypeWindow>*>(out)->push_back({w, r});
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

bool NamedLikeMicrophone(const std::wstring& name)
{
    const std::wstring n = Lower(name);
    if (n.find(L"设置") != std::wstring::npos || n.find(L"setting") != std::wstring::npos)
        return false;
    return n.find(L"语音") != std::wstring::npos || n.find(L"麦克风") != std::wstring::npos || n.find(L"话筒") != std::wstring::npos ||
           n.find(L"voice") != std::wstring::npos || n.find(L"microphone") != std::wstring::npos || n.find(L"mic") == 0;
}

// The input method's voice panel (its window "语音输入", class wetype.flutter.setting) is on screen: a voice
// session is running.
bool VoicePanelOpen()
{
    bool open = false;
    EnumWindows(
        [](HWND w, LPARAM out) -> BOOL {
            if (IsWindowVisible(w) && !Cloaked(w) && Lower(ClassOf(w)).find(L"wetype") != std::wstring::npos &&
                TitleOf(w).find(L"语音") != std::wstring::npos) {
                *reinterpret_cast<bool*>(out) = true;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&open));
    return open;
}

bool WaitForPanel(bool open, DWORD ms)
{
    for (DWORD waited = 0; waited < ms; waited += 50) {
        if (VoicePanelOpen() == open)
            return true;
        Sleep(50);
    }
    return VoicePanelOpen() == open;
}

// Where the microphone on the input method's floating bar is (UI Automation gives its name and place; its Invoke
// answers but does nothing, so it is clicked for real). What UI Automation finds is logged the first time.
bool FindFloatingMicrophone(const std::vector<WeTypeWindow>& windows, bool describe, RECT& where)
{
    if (windows.empty())
        return false;
    Com<IUIAutomation> uia;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                reinterpret_cast<void**>(uia.put()))))
        return false;
    Com<IUIAutomationCondition> all;
    if (FAILED(uia->CreateTrueCondition(all.put())))
        return false;
    for (const WeTypeWindow& window : windows) {
        // The floating bar is small; the voice panel, the candidate list and settings are not what is wanted.
        if (window.rect.bottom - window.rect.top > 200 || window.rect.right - window.rect.left > 900)
            continue;
        Com<IUIAutomationElement> root;
        if (FAILED(uia->ElementFromHandle(window.hwnd, root.put())) || !root)
            continue;
        Com<IUIAutomationElementArray> found;
        if (FAILED(root->FindAll(TreeScope_Descendants, all.p, found.put())) || !found)
            continue;
        int count = 0;
        found->get_Length(&count);
        for (int i = 0; i < count && i < 200; ++i) {
            Com<IUIAutomationElement> e;
            if (FAILED(found->GetElement(i, e.put())) || !e)
                continue;
            BSTR raw = nullptr;
            e->get_CurrentName(&raw);
            const std::wstring name = raw ? raw : L"";
            SysFreeString(raw);
            CONTROLTYPEID type = 0;
            e->get_CurrentControlType(&type);
            RECT box{};
            e->get_CurrentBoundingRectangle(&box);
            if (describe && i < 60)
                Log(L"voice:   uia %p #%d type=%d name=\"%ls\" rect=%ld,%ld,%ld,%ld", static_cast<void*>(window.hwnd), i, int(type),
                    name.c_str(), box.left, box.top, box.right, box.bottom);
            if (NamedLikeMicrophone(name) && box.right > box.left && box.bottom > box.top) {
                where = box;
                return true;
            }
        }
    }
    return false;
}

// The input method's floating bar (class wetype.statusbar.window), shown or hidden.
HWND FloatingBar()
{
    HWND bar = nullptr;
    EnumWindows(
        [](HWND w, LPARAM out) -> BOOL {
            if (Lower(ClassOf(w)) == L"wetype.statusbar.window") {
                *reinterpret_cast<HWND*>(out) = w;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&bar));
    return bar;
}

// Puts a hidden floating bar on screen for a moment (without taking the focus) so its microphone can be
// clicked, and back as it was afterwards.
class ShownForClick {
public:
    explicit ShownForClick(HWND bar) : bar_(bar)
    {
        if (!bar_ || (IsWindowVisible(bar_) && !Cloaked(bar_) && OnScreen()))
            return;
        GetWindowRect(bar_, &was_);
        hidden_ = !IsWindowVisible(bar_);
        if (!OnScreen()) { // somewhere no monitor shows: the bottom right of the main work area, for the click
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            const int w = was_.right - was_.left, h = was_.bottom - was_.top;
            SetWindowPos(bar_, HWND_TOPMOST, work.right - w - 24, work.bottom - h - 24, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            moved_ = true;
        }
        if (hidden_)
            ShowWindow(bar_, SW_SHOWNOACTIVATE);
        Sleep(250); // painted and laid out before its buttons are looked up
        Log(L"voice: put the hidden floating bar on screen for the click (%ls)%ls", Describe(bar_).c_str(),
            Cloaked(bar_) ? L"; it is cloaked, the click may not reach it" : L"");
    }
    ~ShownForClick()
    {
        if (moved_)
            SetWindowPos(bar_, nullptr, was_.left, was_.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (hidden_)
            ShowWindow(bar_, SW_HIDE);
    }
    ShownForClick(const ShownForClick&) = delete;
    ShownForClick& operator=(const ShownForClick&) = delete;

private:
    bool OnScreen() const
    {
        RECT r{};
        GetWindowRect(bar_, &r);
        return r.right > r.left && MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != nullptr;
    }
    HWND bar_;
    RECT was_{};
    bool hidden_ = false, moved_ = false;
};

// A real click at a screen point (the input method ignores clicks that are only posted), the pointer then back
// where it was. The floating bar does not take the focus, so the app keeps it.
void ClickAt(POINT at)
{
    POINT back{};
    GetCursorPos(&back);
    SetCursorPos(at.x, at.y);
    Sleep(30);
    INPUT click[2]{};
    click[0].type = click[1].type = INPUT_MOUSE;
    click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &click[0], sizeof(INPUT));
    Sleep(40);
    SendInput(1, &click[1], sizeof(INPUT));
    Sleep(30);
    SetCursorPos(back.x, back.y);
}

// The hotkey Ctrl+Win+Shift, as the keyboard sends it: scan codes with the virtual keys, all three down
// together for a moment, then up in reverse order (Shift still down while Win goes up, so no Start menu).
void SendHotkey()
{
    const WORD keys[] = {VK_LCONTROL, VK_LWIN, VK_LSHIFT};
    INPUT down[3]{}, up[3]{};
    for (int i = 0; i < 3; ++i) {
        for (INPUT* in : {&down[i], &up[2 - i]}) {
            in->type = INPUT_KEYBOARD;
            in->ki.wVk = keys[i];
            in->ki.wScan = WORD(MapVirtualKeyW(keys[i], MAPVK_VK_TO_VSC));
            in->ki.dwFlags = keys[i] == VK_LWIN ? KEYEVENTF_EXTENDEDKEY : 0;
        }
        up[2 - i].ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    UINT sent = 0;
    for (const INPUT& in : down) {
        sent += SendInput(1, const_cast<INPUT*>(&in), sizeof(INPUT));
        Sleep(20);
    }
    Sleep(120);
    for (const INPUT& in : up) {
        sent += SendInput(1, const_cast<INPUT*>(&in), sizeof(INPUT));
        Sleep(20);
    }
    if (sent != 6)
        Log(L"voice: SendInput delivered %u of 6 key events (%lu): the app in front may run as administrator", sent, GetLastError());
    else
        Log(L"voice: sent Ctrl+Win+Shift");
}

} // namespace

bool VoiceSessionOpen() { return VoicePanelOpen(); }

void StartVoiceInput(HWND lastApp)
{
    static std::atomic<bool> busy{false};
    static std::atomic<bool> described{false};
    if (busy.exchange(true))
        return;
    std::thread([lastApp] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        GiveKeyboardBack(lastApp);
        const HWND fg = GetForegroundWindow();
        const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(fg, nullptr));
        if (PRIMARYLANGID(LOWORD(reinterpret_cast<ULONG_PTR>(layout))) != LANG_CHINESE)
            Log(L"voice: the app in front is not using a Chinese input method (layout %04X)",
                unsigned(LOWORD(reinterpret_cast<ULONG_PTR>(layout))));
        // The floating bar's microphone in every case: a hidden bar is put on screen for the click.
        const HWND bar = FloatingBar();
        if (!bar)
            Log(L"voice: the input method's floating bar does not exist (is WeChat Input Method running?)");
        ShownForClick shown(bar);
        const std::vector<WeTypeWindow> windows = VisibleWeTypeWindows();
        const bool describe = !described.exchange(true);
        Log(L"voice: %zu WeType window(s) visible", windows.size());
        for (size_t i = 0; i < windows.size() && i < 12; ++i)
            Log(L"voice:   %ls", Describe(windows[i].hwnd).c_str());
        // The floating bar's microphone (some apps, e.g. the bilibili client, ignore the hotkey but not this button);
        // when the click changed nothing, the hotkey.
        const bool wasOpen = VoicePanelOpen();
        const wchar_t* const outcome = wasOpen ? L"closed" : L"opened";
        RECT mic{};
        bool done = false;
        if (FindFloatingMicrophone(windows, describe, mic)) {
            ClickAt(POINT{(mic.left + mic.right) / 2, (mic.top + mic.bottom) / 2});
            done = WaitForPanel(!wasOpen, 900);
            Log(L"voice: clicked the floating bar's microphone: the voice panel %ls", done ? outcome : L"did not change");
        }
        if (!done) {
            SendHotkey();
            done = WaitForPanel(!wasOpen, 900);
            Log(L"voice: after the hotkey the voice panel %ls", done ? outcome : L"did not change");
        }
        if (SUCCEEDED(com))
            CoUninitialize();
        busy = false;
    }).detach();
}

} // namespace atrium
