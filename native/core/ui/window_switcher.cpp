#include "ui/window_switcher.h"

#include "log.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace atrium {

namespace {

// DIP. As the window previews: a title row, then the window's picture; cards side by side.
constexpr float kRadius = 22, kPad = 12, kGap = 8, kCardPad = 10, kTitleH = 28, kImageH = 132, kMinImageW = 150, kMaxImageW = 236;
constexpr float kCardR = kRadius - kPad;

std::wstring TitleOf(HWND h)
{
    wchar_t buf[256] = {}; // InternalGetWindowText sends no message: a hung window cannot stall this
    int n = InternalGetWindowText(h, buf, 256);
    return std::wstring(buf, size_t(std::max(n, 0)));
}

} // namespace

WindowSwitcher::WindowSwitcher(HINSTANCE instance) : surface_(instance, L"Atrium Window Switcher", false)
{
    surface_.paint = [this](Canvas& c) { Paint(c); };
    surface_.message = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return Message(m, w, l, r); };
    surface_.onDismiss = [this] { Cancel(); };
    surface_.onReady = [this] {
        ready_ = true;
        UpdateThumbs();
    };
    surface_.SetRadius(kRadius);
}

WindowSwitcher::~WindowSwitcher() { Close(); }

void WindowSwitcher::Close()
{
    for (Card& c : cards_)
        if (c.thumb)
            DwmUnregisterThumbnail(c.thumb);
    cards_.clear();
    ready_ = false;
    if (surface_.Visible())
        surface_.Hide();
}

bool WindowSwitcher::Open(std::vector<Entry> entries, bool back)
{
    Close();
    for (Entry& e : entries) {
        if (!IsWindow(e.window))
            continue;
        Card c;
        c.title = TitleOf(e.window);
        c.entry = std::move(e);
        cards_.push_back(std::move(c));
    }
    if (cards_.size() < 2) {
        cards_.clear();
        return false;
    }
    selected_ = back ? int(cards_.size()) - 1 : 1;
    for (Card& c : cards_) {
        if (FAILED(DwmRegisterThumbnail(surface_.Hwnd(), c.entry.window, &c.thumb)))
            c.thumb = nullptr;
        SIZE source{1, 1};
        if (c.thumb)
            DwmQueryThumbnailSourceSize(c.thumb, &source);
        RECT win{}, frame{};
        GetWindowRect(c.entry.window, &win);
        c.sourceRect = RECT{0, 0, source.cx, source.cy};
        if (!IsIconic(c.entry.window) && SUCCEEDED(DwmGetWindowAttribute(c.entry.window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))) &&
            win.right > win.left) {
            c.sourceRect = RECT{frame.left - win.left, frame.top - win.top, std::min<LONG>(frame.right - win.left, source.cx),
                                std::min<LONG>(frame.bottom - win.top, source.cy)};
        }
    }
    // In the middle of the monitor the pointer is on.
    POINT pt{};
    GetCursorPos(&pt);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& work = mi.rcWork;
    const float s = PopupScale(work);
    Layout(float(work.right - work.left) / s - 48);
    const LONG w = LONG(std::lround(size_.width * s)), h = LONG(std::lround(size_.height * s));
    const LONG x = work.left + (work.right - work.left - w) / 2, y = work.top + (work.bottom - work.top - h) / 2;
    surface_.Show(RECT{x, y, x + w, y + h}, false, POINT{x + w / 2, y + h / 2});
    surface_.Invalidate();
    Log(L"switcher: %zu window(s)", cards_.size());
    return true;
}

void WindowSwitcher::Layout(float maxWidthDip)
{
    float imageH = kImageH;
    auto widthOf = [&](const Card& c, float h) {
        const float w = float(std::max<LONG>(1, c.sourceRect.right - c.sourceRect.left)), hh = float(std::max<LONG>(1, c.sourceRect.bottom - c.sourceRect.top));
        return std::clamp(h * w / hh, kMinImageW * h / kImageH, kMaxImageW * h / kImageH);
    };
    auto total = [&](float h) {
        float sum = 2 * kPad + float(cards_.size() - 1) * kGap;
        for (const Card& c : cards_)
            sum += widthOf(c, h) + 2 * kCardPad;
        return sum;
    };
    while (imageH > 44 && total(imageH) > maxWidthDip)
        imageH -= 4;
    float x = kPad;
    for (Card& c : cards_) {
        const float w = widthOf(c, imageH) + 2 * kCardPad;
        c.rect = D2D1::RectF(x, kPad, x + w, kPad + kCardPad + kTitleH + imageH + kCardPad);
        c.image = D2D1::RectF(c.rect.left + kCardPad, c.rect.top + kCardPad + kTitleH, c.rect.right - kCardPad, c.rect.bottom - kCardPad);
        x += w + kGap;
    }
    size_ = D2D1::SizeF(x - kGap + kPad, kPad + kCardPad + kTitleH + imageH + kCardPad + kPad);
}

void WindowSwitcher::UpdateThumbs()
{
    const float s = surface_.Scale();
    for (Card& c : cards_) {
        if (!c.thumb)
            continue;
        const float sw = float(std::max<LONG>(1, c.sourceRect.right - c.sourceRect.left)), sh = float(std::max<LONG>(1, c.sourceRect.bottom - c.sourceRect.top));
        const float bw = c.image.right - c.image.left, bh = c.image.bottom - c.image.top;
        const float k = std::min(bw / sw, bh / sh), dw = sw * k, dh = sh * k;
        const float l = c.image.left + (bw - dw) / 2, t = c.image.top + (bh - dh) / 2;
        POINT tl = surface_.ContentToScreen(POINT{LONG(std::lround(l * s)), LONG(std::lround(t * s))});
        ScreenToClient(surface_.Hwnd(), &tl);
        DWM_THUMBNAIL_PROPERTIES p{};
        p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        p.rcDestination = RECT{tl.x, tl.y, tl.x + LONG(std::lround(dw * s)), tl.y + LONG(std::lround(dh * s))};
        p.rcSource = c.sourceRect;
        p.fVisible = ready_;
        p.opacity = 255;
        p.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(c.thumb, &p);
    }
}

void WindowSwitcher::Paint(Canvas& c)
{
    const Theme& t = c.theme;
    if (c.panel)
        c.Fill(D2D1::RectF(0, 0, size_.width, size_.height), kRadius, t.panel);
    for (size_t i = 0; i < cards_.size(); ++i) {
        Card& card = cards_[i];
        const float on = card.chosen.Step(int(i) == selected_, c.now);
        if (card.chosen.moving)
            c.Continue();
        if (on > 0.001f) // the selected card: the hover wash, a little stronger
            c.Fill(card.rect, kCardR, D2D1::ColorF(t.hover.r, t.hover.g, t.hover.b, std::min(1.f, t.hover.a * 1.6f) * on));
        const float ty = card.rect.top + kCardPad, cy = ty + kTitleH / 2 - 2;
        float textLeft = card.rect.left + kCardPad + 2;
        if (!card.entry.icon.empty()) {
            c.Image(L"switcher|" + card.entry.key, card.entry.icon, D2D1::RectF(textLeft, cy - 8, textLeft + 16, cy + 8));
            textLeft += 22;
        }
        c.Text(card.title.empty() ? L"窗口" : card.title, D2D1::RectF(textLeft, ty - 2, card.rect.right - kCardPad - 2, ty + kTitleH - 2), 12.5f, t.text,
               DWRITE_FONT_WEIGHT_MEDIUM);
        if (!card.thumb) { // no picture from DWM: the app's icon on a card
            c.Fill(card.image, kCardR - 4, t.card);
            const float ic = std::min(48.f, (card.image.bottom - card.image.top) * 0.5f), mx = (card.image.left + card.image.right) / 2,
                        my = (card.image.top + card.image.bottom) / 2;
            if (!card.entry.icon.empty())
                c.Image(L"switcher|" + card.entry.key, card.entry.icon, D2D1::RectF(mx - ic / 2, my - ic / 2, mx + ic / 2, my + ic / 2), 0.9f);
        }
    }
    UpdateThumbs();
}

void WindowSwitcher::Step(bool back)
{
    if (cards_.empty())
        return;
    const int n = int(cards_.size());
    selected_ = (selected_ + (back ? n - 1 : 1)) % n;
    surface_.Invalidate();
}

void WindowSwitcher::Commit()
{
    if (!surface_.Visible() || cards_.empty())
        return;
    const HWND window = cards_[size_t(std::clamp(selected_, 0, int(cards_.size()) - 1))].entry.window;
    Close();
    if (onActivate && IsWindow(window))
        onActivate(window);
}

void WindowSwitcher::Cancel() { Close(); }

bool WindowSwitcher::Message(UINT m, WPARAM, LPARAM l, LRESULT&)
{
    if (m != WM_LBUTTONUP && m != WM_MOUSEMOVE)
        return false;
    const float s = surface_.Scale(), x = float(GET_X_LPARAM(l)) / s, y = float(GET_Y_LPARAM(l)) / s;
    for (size_t i = 0; i < cards_.size(); ++i) {
        const D2D1_RECT_F& r = cards_[i].rect;
        if (x < r.left || x >= r.right || y < r.top || y >= r.bottom)
            continue;
        if (m == WM_LBUTTONUP) {
            selected_ = int(i);
            Commit();
        }
        return true;
    }
    return m == WM_LBUTTONUP;
}

} // namespace atrium
