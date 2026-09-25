#include "ui.hpp"

#include <algorithm>
#include <cstdlib>

#include "font_data.hpp"

namespace editor {

uint32_t utf8Next(const std::string& s, size_t& i) {
    uint8_t c = uint8_t(s[i++]);
    if (c < 0x80) return c;
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = c & (0x3F >> n);
    for (int k = 0; k < n && i < s.size(); k++) cp = cp << 6 | (uint8_t(s[i++]) & 0x3F);
    return cp;
}

std::string utf8Encode(uint32_t cp) {
    std::string o;
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) { o += char(0xC0 | cp >> 6); o += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += char(0xE0 | cp >> 12); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    else {
        o += char(0xF0 | cp >> 18); o += char(0x80 | ((cp >> 12) & 0x3F));
        o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F));
    }
    return o;
}

size_t Ui::utf8Length(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) { utf8Next(s, i); n++; }
    return n;
}

std::string Ui::utf8PopBack(const std::string& s) {
    if (s.empty()) return s;
    size_t i = s.size() - 1;
    while (i > 0 && (uint8_t(s[i]) & 0xC0) == 0x80) i--;
    return s.substr(0, i);
}

bool Ui::init(SDL_Renderer* r) {
    r_ = r;
    // font atlas: all glyphs in one row, white with alpha
    std::vector<uint32_t> px(size_t(kGlyphCount * kGlyphW * kGlyphH));
    for (int g = 0; g < kGlyphCount; g++)
        for (int y = 0; y < kGlyphH; y++)
            for (int x = 0; x < kGlyphW; x++)
                px[size_t(y * kGlyphCount * kGlyphW + g * kGlyphW + x)] =
                    uint32_t(kGlyphAlpha[g][y * kGlyphW + x]) << 24 | 0xFFFFFFu;
    font_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, kGlyphCount * kGlyphW, kGlyphH);
    if (!font_) return false;
    SDL_UpdateTexture(font_, nullptr, px.data(), kGlyphCount * kGlyphW * 4);
    SDL_SetTextureBlendMode(font_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    return true;
}

void Ui::destroy() {
    if (font_) SDL_DestroyTexture(font_);
    font_ = nullptr;
}

void Ui::beginFrame() {
    for (int i = 0; i < 3; i++) { pressed[i] = released[i] = false; }
    wheelX = wheelY = 0;
    typed.clear();
    keys.clear();
    doubleClick = false;
    captured = false;
    tip_.clear();
}

void Ui::handleEvent(const SDL_Event& e, float scale) {
    auto btn = [](int b) { return b == SDL_BUTTON_LEFT ? 0 : b == SDL_BUTTON_MIDDLE ? 1 : b == SDL_BUTTON_RIGHT ? 2 : -1; };
    switch (e.type) {
    case SDL_MOUSEMOTION:
        mx = int(e.motion.x / scale);
        my = int(e.motion.y / scale);
        break;
    case SDL_MOUSEBUTTONDOWN: {
        int b = btn(e.button.button);
        mx = int(e.button.x / scale);
        my = int(e.button.y / scale);
        if (b >= 0) { down[b] = true; pressed[b] = true; }
        if (b == 0) {
            Uint32 t = SDL_GetTicks();
            doubleClick = t - lastClick_ < 350 && std::abs(mx - lastClickX_) < 4 && std::abs(my - lastClickY_) < 4;
            lastClick_ = t; lastClickX_ = mx; lastClickY_ = my;
        }
        break;
    }
    case SDL_MOUSEBUTTONUP: {
        int b = btn(e.button.button);
        if (b >= 0) { down[b] = false; released[b] = true; }
        break;
    }
    case SDL_MOUSEWHEEL:
        wheelY += e.wheel.y;
        wheelX += e.wheel.x;
        break;
    case SDL_TEXTINPUT:
        typed += e.text.text;
        break;
    case SDL_KEYDOWN:
        keys.push_back(e.key.keysym);
        break;
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) down[0] = down[1] = down[2] = false;
        break;
    default: break;
    }
}

void Ui::endFrame() {
    if (tip_.empty()) return;
    int w = textWidth(tip_) + 12, h = 22;
    int x = mx + 14, y = my + 18;
    int vw = 0, vh = 0;
    SDL_RenderGetLogicalSize(r_, &vw, &vh);
    if (vw == 0) {
        int ow, oh;
        SDL_GetRendererOutputSize(r_, &ow, &oh);
        float sx, sy;
        SDL_RenderGetScale(r_, &sx, &sy);
        vw = int(ow / sx); vh = int(oh / sy);
    }
    if (x + w > vw) x = std::max(0, vw - w);
    if (y + h > vh) y = my - h - 4;
    fill({x, y, w, h}, {20, 22, 27, 240});
    frame({x, y, w, h}, theme::line);
    text(x + 6, y + 3, tip_);
}

void Ui::fill(Rect rc, Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect q{rc.x, rc.y, rc.w, rc.h};
    SDL_RenderFillRect(r_, &q);
}

void Ui::frame(Rect rc, Color c, int t) {
    for (int i = 0; i < t; i++) {
        SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
        SDL_Rect q{rc.x + i, rc.y + i, rc.w - 2 * i, rc.h - 2 * i};
        SDL_RenderDrawRect(r_, &q);
    }
}

void Ui::line(int x1, int y1, int x2, int y2, Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r_, x1, y1, x2, y2);
}

int Ui::glyphIndex(uint32_t cp) const {
    // the table is sorted except for the tail: linear search is fine (177 glyphs)
    for (int i = 0; i < kGlyphCount; i++)
        if (kGlyphCodes[i] == cp) return i;
    return '?' - 32;
}

int Ui::text(int x, int y, const std::string& s, Color c) {
    SDL_SetTextureColorMod(font_, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(font_, c.a);
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = utf8Next(s, i);
        int g = glyphIndex(cp);
        SDL_Rect src{g * kGlyphW, 0, kGlyphW, kGlyphH}, dst{x, y, kGlyphW, kGlyphH};
        SDL_RenderCopy(r_, font_, &src, &dst);
        x += kGlyphW;
    }
    return x;
}

int Ui::textWidth(const std::string& s) const { return int(utf8Length(s)) * kGlyphW; }

void Ui::textCentered(Rect rc, const std::string& s, Color c) {
    text(rc.x + (rc.w - textWidth(s)) / 2, rc.y + (rc.h - kGlyphH) / 2, s, c);
}

void Ui::textClipped(Rect rc, const std::string& s, Color c) {
    int maxChars = rc.w / kGlyphW;
    if (int(utf8Length(s)) <= maxChars) { text(rc.x, rc.y + (rc.h - kGlyphH) / 2, s, c); return; }
    std::string o;
    size_t i = 0;
    for (int n = 0; n < maxChars - 1 && i < s.size(); n++) o += utf8Encode(utf8Next(s, i));
    text(rc.x, rc.y + (rc.h - kGlyphH) / 2, o + "…", c);
}

void Ui::setClip(const Rect* rc) {
    if (!rc) { SDL_RenderSetClipRect(r_, nullptr); return; }
    SDL_Rect q{rc->x, rc->y, rc->w, rc->h};
    SDL_RenderSetClipRect(r_, &q);
}

bool Ui::hover(Rect rc) const { return !modal && rc.contains(mx, my); }

bool Ui::button(Rect rc, const std::string& label, bool enabled, bool selected, const std::string& tip) {
    bool h = enabled && hover(rc);
    Color c = selected ? theme::accentDim : !enabled ? theme::panel2 : h && down[0] ? theme::buttonDown : h ? theme::buttonHot : theme::button;
    fill(rc, c);
    frame(rc, selected ? theme::accent : theme::line);
    textCentered(rc, label, enabled ? theme::text : theme::dim);
    if (hover(rc) && !tip.empty()) tooltip(tip);
    if (h && pressed[0]) { captured = true; return true; }
    return false;
}

bool Ui::checkbox(Rect rc, const std::string& label, bool& v, const std::string& tip) {
    Rect box{rc.x, rc.y + (rc.h - 14) / 2, 14, 14};
    bool h = hover(rc);
    fill(box, h ? theme::buttonHot : theme::button);
    frame(box, v ? theme::accent : theme::line);
    if (v) textCentered(box, "✓", theme::accent);
    text(rc.x + 20, rc.y + (rc.h - 16) / 2, label);
    if (h && !tip.empty()) tooltip(tip);
    if (h && pressed[0]) { v = !v; captured = true; return true; }
    return false;
}

bool Ui::textField(int id, Rect rc, std::string& v, size_t maxChars, const std::function<uint32_t(uint32_t)>& filter,
                   const std::string& tip) {
    bool h = hover(rc);
    bool changed = false;
    if (h && pressed[0]) {
        if (focus != id) selectAll_ = true;  // the first key replaces the whole text
        focus = id; captured = true; SDL_StartTextInput(); caretTime_ = SDL_GetTicks();
    }
    else if (pressed[0] && focus == id && !modal) { focus = 0; }
    bool f = focus == id;
    fill(rc, f ? Color{24, 26, 31} : theme::panel2);
    frame(rc, f ? theme::accent : theme::line);
    if (f) {
        for (size_t i = 0; i < typed.size();) {
            uint32_t cp = filter(utf8Next(typed, i));
            if (cp && selectAll_) { v.clear(); selectAll_ = false; }
            if (cp && utf8Length(v) < maxChars) { v += utf8Encode(cp); changed = true; }
        }
        for (const SDL_Keysym& k : keys) {
            if (k.sym == SDLK_BACKSPACE && selectAll_) { v.clear(); selectAll_ = false; changed = true; continue; }
            if (k.sym == SDLK_BACKSPACE && !v.empty()) { v = utf8PopBack(v); changed = true; }
            if (k.sym == SDLK_LEFT || k.sym == SDLK_RIGHT || k.sym == SDLK_END || k.sym == SDLK_HOME) selectAll_ = false;
            if (k.sym == SDLK_RETURN || k.sym == SDLK_KP_ENTER || k.sym == SDLK_ESCAPE || k.sym == SDLK_TAB) focus = 0;
        }
    }
    Rect inner{rc.x + 4, rc.y, rc.w - 8, rc.h};
    if (f && selectAll_ && !v.empty()) fill({inner.x - 1, rc.y + 3, textWidth(v) + 2, rc.h - 6}, theme::accentDim);
    int end = text(inner.x, rc.y + (rc.h - 16) / 2, v);
    if (f && ((SDL_GetTicks() - caretTime_) / 500) % 2 == 0) fill({end, rc.y + 3, 1, rc.h - 6}, theme::accent);
    if (h && !tip.empty()) tooltip(tip);
    return changed;
}

}  // namespace editor
