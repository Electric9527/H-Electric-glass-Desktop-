#include "shell/app_message_source.h"

#include "log.h"
#include "shell/uia_client.h"

#include <tlhelp32.h>
#include <uiautomation.h>

#include <winrt/base.h>

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>

namespace atrium {

AppMessageSpec QQSpec() { return {L"qq", L"QQ", {L"QQ.exe"}}; }
AppMessageSpec BilibiliSpec() { return {L"bilibili", L"哔哩哔哩", {L"哔哩哔哩.exe", L"bilibili.exe"}}; }

const AppMessageSpec* AppMessageSpecFor(const std::wstring& exePath)
{
    static const AppMessageSpec specs[] = {QQSpec(), BilibiliSpec()};
    std::wstring name = exePath.substr(exePath.find_last_of(L"\\/") == std::wstring::npos ? 0 : exePath.find_last_of(L"\\/") + 1);
    for (const AppMessageSpec& s : specs)
        for (const std::wstring& exe : s.exes)
            if (_wcsicmp(name.c_str(), exe.c_str()) == 0)
                return &s;
    return nullptr;
}

namespace {

constexpr DWORD kReconcileMs = 5000;
constexpr ULONGLONG kProcessesMs = 10000; // the app's processes are listed again after this
constexpr ULONGLONG kUnknownMs = 3000;    // or sooner when a popup-like window is not theirs
constexpr ULONGLONG kPopupLifeMs = 60000; // a message popup goes within seconds: a small corner window up longer (a mini player) is not one

std::wstring Trim(std::wstring s)
{
    while (!s.empty() && iswspace(s.back()))
        s.pop_back();
    size_t a = s.find_first_not_of(L" \t\r\n");
    return a == std::wstring::npos ? std::wstring() : s.substr(a);
}

// The app's processes: id -> program path.
std::map<DWORD, std::wstring> Processes(const AppMessageSpec& spec)
{
    std::map<DWORD, std::wstring> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        bool match = false;
        for (const std::wstring& exe : spec.exes)
            match = match || _wcsicmp(pe.szExeFile, exe.c_str()) == 0;
        if (!match)
            continue;
        std::wstring path;
        if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
            wchar_t buf[MAX_PATH * 2];
            DWORD len = DWORD(std::size(buf));
            if (QueryFullProcessImageNameW(p, 0, buf, &len))
                path = buf;
            CloseHandle(p);
        }
        out[pe.th32ProcessID] = path;
    }
    CloseHandle(snap);
    return out;
}

struct Window {
    HWND hwnd;
    DWORD pid;
    bool popup; // small, at a corner of the work area: a message popup
    std::wstring title, cls;
    float w, h; // DIP
};

// The app's visible windows (all of them, so the unknown ones can be described once); `unknown`: a
// popup-like Chromium window of a process not in `procs`.
std::vector<Window> AppWindows(const std::map<DWORD, std::wstring>& procs, bool& unknown)
{
    struct Ctx {
        const std::map<DWORD, std::wstring>* procs;
        std::vector<Window> out;
        bool unknown = false;
    } ctx{&procs, {}};
    EnumWindows(
        [](HWND h, LPARAM l) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(l);
            if (!IsWindowVisible(h) || IsIconic(h))
                return TRUE;
            wchar_t cls[64] = {};
            GetClassNameW(h, cls, 64);
            RECT r{};
            GetWindowRect(h, &r);
            float s = float(GetDpiForWindow(h)) / 96.f;
            if (s <= 0)
                s = 1;
            const float w = float(r.right - r.left) / s, hh = float(r.bottom - r.top) / s;
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
            const float right = float(mi.rcWork.right - r.right) / s, left = float(r.left - mi.rcWork.left) / s;
            const float bottom = float(mi.rcWork.bottom - r.bottom) / s, top = float(r.top - mi.rcWork.top) / s;
            const bool corner = (right <= 64 || left <= 64) && (bottom <= 64 || top <= 64);
            const bool popup = corner && w >= 140 && w <= 560 && hh >= 40 && hh <= 320;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (!c->procs->count(pid)) {
                if (popup && wcsstr(cls, L"Chrome_WidgetWin"))
                    c->unknown = true;
                return TRUE;
            }
            // InternalGetWindowText sends no message (GetWindowText would wait for the window's thread).
            wchar_t title[128] = {};
            InternalGetWindowText(h, title, 128);
            c->out.push_back({h, pid, popup, title, cls, w, hh});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    unknown = ctx.unknown;
    return ctx.out;
}

// The window's texts in order, the empty ones left out.
std::vector<std::wstring> Texts(IUIAutomation* uia, IUIAutomationCondition* text, IUIAutomationCacheRequest* cache, HWND h, UiaWatch& watch)
{
    std::vector<std::wstring> out;
    winrt::com_ptr<IUIAutomationElement> root;
    winrt::com_ptr<IUIAutomationElementArray> all;
    if (FAILED(uia->ElementFromHandle(h, root.put())) || !root)
        return out;
    watch.Add(h, root.get());
    if (FAILED(root->FindAllBuildCache(TreeScope_Descendants, text, cache, all.put())) || !all)
        return out;
    int n = 0;
    all->get_Length(&n);
    for (int i = 0; i < n && i < 16; ++i) {
        winrt::com_ptr<IUIAutomationElement> e;
        all->GetElement(i, e.put());
        BSTR name = nullptr;
        if (e && SUCCEEDED(e->get_CachedName(&name)) && name) {
            std::wstring t = Trim(name);
            if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end())
                out.push_back(t);
        }
        SysFreeString(name);
    }
    return out;
}

} // namespace

AppMessageSource::AppMessageSource(HWND notify, UINT msg, AppMessageSpec spec) : notify_(notify), msg_(msg), spec_(std::move(spec))
{
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = std::thread([this] { Run(); });
}

AppMessageSource::~AppMessageSource()
{
    stop_ = true;
    SetEvent(wake_);
    thread_.join();
    CloseHandle(wake_);
}

void AppMessageSource::Run()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const std::wstring who = spec_.source;
    winrt::com_ptr<IUIAutomationCondition> text;
    winrt::com_ptr<IUIAutomationCacheRequest> cache;
    winrt::com_ptr<IUIAutomation> uia = CreateUia(stop_, wake_, who.c_str(), [&](IUIAutomation* u) {
        text = nullptr;
        VARIANT v{};
        v.vt = VT_I4;
        v.lVal = UIA_TextControlTypeId;
        HRESULT hr = u->CreatePropertyCondition(UIA_ControlTypePropertyId, v, text.put());
        if (FAILED(hr))
            return hr;
        cache = nullptr;
        winrt::com_ptr<IUIAutomationCondition> raw;
        if (FAILED(hr = u->CreateCacheRequest(cache.put())) || FAILED(hr = cache->AddProperty(UIA_NamePropertyId)) ||
            FAILED(hr = cache->put_TreeScope(TreeScope_Element)) || FAILED(hr = u->get_RawViewCondition(raw.put())) ||
            FAILED(hr = cache->put_TreeFilter(raw.get())))
            return hr;
        return cache->put_AutomationElementMode(AutomationElementMode_None);
    });
    std::map<HWND, std::wstring> seen; // popups handled, by window and text (a popup may be reused)
    std::map<HWND, int> waiting;       // popups whose text has not loaded: passes so far
    std::set<HWND> described;          // windows already described in the log
    std::map<HWND, ULONGLONG> shown;   // popup-like windows: when they were first seen on screen
    bool baseline = false, running = false;
    std::map<DWORD, std::wstring> procs;
    ULONGLONG listed = 0;
    {
        UiaWatch watch(uia.get(), wake_);
        UiaWindowWake windows(wake_, L"Chrome_WidgetWin");
        while (!stop_ && uia && text) {
            bool loading = false;
            try {
                watch.Prune();
                ULONGLONG tick = GetTickCount64();
                bool unknown = false;
                auto all = AppWindows(procs, unknown);
                if (!listed || tick - listed >= kProcessesMs || (unknown && tick - listed >= kUnknownMs)) {
                    procs = Processes(spec_);
                    windows.SetProcesses(procs);
                    listed = tick;
                    all = AppWindows(procs, unknown);
                    if (procs.empty() != !running) {
                        running = !procs.empty();
                        Log(L"%s: %s", who.c_str(), running ? L"watching the app's message popups" : L"the app is not running");
                    }
                }
                std::map<HWND, std::wstring> now;
                std::map<HWND, int> stillWaiting;
                std::map<HWND, ULONGLONG> stillShown;
                for (const Window& win : all) {
                    if (described.insert(win.hwnd).second)
                        Log(L"%s: window %p class=%s title=\"%s\" %.0fx%.0f DIP%s", who.c_str(), static_cast<void*>(win.hwnd), win.cls.c_str(),
                            win.title.c_str(), win.w, win.h, win.popup ? L" (a message popup)" : L"");
                    if (!win.popup)
                        continue;
                    const ULONGLONG since = shown.count(win.hwnd) ? shown[win.hwnd] : tick;
                    stillShown[win.hwnd] = since;
                    if (tick - since > kPopupLifeMs)
                        continue;
                    auto old = seen.find(win.hwnd);
                    const bool firstRead = !watch.Contains(win.hwnd);
                    std::vector<std::wstring> t = Texts(uia.get(), text.get(), cache.get(), win.hwnd, watch);
                    if (t.empty()) {
                        const int passes = waiting.count(win.hwnd) ? waiting[win.hwnd] + 1 : 1;
                        if (passes < 3 || win.title.empty()) {
                            // Still loading: read again (startup popups stay in the baseline meanwhile).
                            if (!baseline || old != seen.end())
                                now[win.hwnd] = old != seen.end() ? old->second : L"";
                            stillWaiting[win.hwnd] = passes;
                            loading = loading || firstRead || passes < 3;
                            continue;
                        }
                        t.push_back(win.title); // no readable text: its title at least
                    }
                    std::wstring key;
                    for (const auto& s : t)
                        key += s + L"\n";
                    now[win.hwnd] = key;
                    if (!baseline || (old != seen.end() && (old->second.empty() || old->second == key)))
                        continue;
                    auto* n = new Notice();
                    n->source = spec_.source;
                    n->app = spec_.app;
                    n->appId = procs.count(win.pid) ? procs.at(win.pid) : L"";
                    n->title = t[0];
                    for (size_t i = 1; i < t.size(); ++i)
                        n->body += (n->body.empty() ? L"" : L" ") + t[i];
                    if (n->body.empty())
                        n->body = L"收到新消息";
                    if (!n->appId.empty())
                        n->action = L"app:" + n->appId;
                    if (!PostMessageW(notify_, msg_, 0, reinterpret_cast<LPARAM>(n))) {
                        delete n;
                    } else {
                        lastPosted_ = GetTickCount64();
                        Log(L"%s: new message popup", who.c_str());
                    }
                }
                seen = std::move(now);
                waiting = std::move(stillWaiting);
                shown = std::move(stillShown);
                for (auto it = described.begin(); it != described.end();) // forget closed windows
                    it = IsWindow(*it) ? std::next(it) : described.erase(it);
                baseline = true;
            } catch (...) {
                Log(L"%s: reading failed", who.c_str());
            }
            if (!stop_)
                WaitForUiaWake(wake_, loading ? 300 : kReconcileMs);
        }
    }
    text = nullptr;
    cache = nullptr;
    uia = nullptr;
    winrt::uninit_apartment();
}

} // namespace atrium
