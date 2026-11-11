// Messages of apps that show their own popup instead of a Windows notification, as Atrium cards: QQ and
// the bilibili client (哔哩哔哩), both Chromium windows, the way FeishuSource reads Feishu. A message popup
// is a small window of the app's process (140 x 40 to 560 x 320 DIP) at a corner of the work area; its texts
// are read through UI Automation, read only: the first is the sender or chat, the rest the message. Window and
// UIA events wake an MTA reader; a five-second reconciliation covers missed events. Popups on screen when it
// starts are the baseline. Each window of the app it has not seen before is described once in core.log
// ("<source>: window …"), so another version's popup can be recognised.
// Apps that only flash their taskbar button are covered in main.cpp: a flashing QQ or bilibili window posts
// a short card, unless this reader posted one for the app a moment before.
#pragma once

#include "ui/notifications.h"

#include <windows.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace atrium {

struct AppMessageSpec {
    std::wstring source;            // Notice::source, e.g. "qq"
    std::wstring app;               // the card's app name, e.g. "QQ"
    std::vector<std::wstring> exes; // program file names, compared without case
};

// The specs of the apps read here.
AppMessageSpec QQSpec();
AppMessageSpec BilibiliSpec();
// The spec whose program `exePath` is (a path or a lowercase task key), or nullptr.
const AppMessageSpec* AppMessageSpecFor(const std::wstring& exePath);

class AppMessageSource {
public:
    // Posts `msg` to `notify` with a heap-allocated Notice* in lParam (the receiver deletes it).
    AppMessageSource(HWND notify, UINT msg, AppMessageSpec spec);
    ~AppMessageSource();
    AppMessageSource(const AppMessageSource&) = delete;
    AppMessageSource& operator=(const AppMessageSource&) = delete;

    // GetTickCount64 of the last card this reader posted (0: none).
    ULONGLONG LastPosted() const { return lastPosted_; }

private:
    void Run();
    HWND notify_;
    UINT msg_;
    AppMessageSpec spec_;
    std::atomic<bool> stop_{false};
    std::atomic<ULONGLONG> lastPosted_{0};
    HANDLE wake_ = nullptr;
    std::thread thread_;
};

} // namespace atrium
