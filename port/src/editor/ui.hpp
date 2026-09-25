// Small immediate-mode UI on top of SDL_Renderer for the level editor:
// UTF-8 text with the built-in font, buttons, check boxes, text fields,
// tooltips. Coordinates are logical pixels (the window size divided by the
// UI scale).
#pragma once
#include <SDL.h>

#include <functional>
#include <string>
#include <vector>

namespace editor {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect shrink(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

struct Color { uint8_t r, g, b, a = 255; };

namespace theme {
constexpr Color bg{30, 32, 38}, panel{40, 43, 51}, panel2{50, 54, 64}, line{70, 75, 88};
constexpr Color text{220, 223, 230}, dim{140, 146, 160}, accent{86, 156, 255}, accentDim{50, 90, 150};
constexpr Color button{58, 62, 74}, buttonHot{72, 78, 94}, buttonDown{44, 48, 58};
constexpr Color error{235, 80, 70}, warning{240, 190, 60}, info{120, 170, 230}, ok{110, 200, 120};
}  // namespace theme

class Ui {
public:
    bool init(SDL_Renderer* r);
    void destroy();

    // ---- frame / input -------------------------------------------------
    void beginFrame();
    void handleEvent(const SDL_Event& e, float scale);  // scale: window px per logical px
    void endFrame();  // draws the tooltip

    int mx = 0, my = 0;
    bool down[3] = {}, pressed[3] = {}, released[3] = {};  // left, middle, right
    int wheelY = 0, wheelX = 0;
    std::string typed;                  // text typed this frame (UTF-8)
    std::vector<SDL_Keysym> keys;       // keys pressed this frame
    bool doubleClick = false;

    bool modal = false;                 // a modal dialog blocks the widgets below
    int focus = 0;                      // id of the focused text field (0 = none)
    bool captured = false;              // set when a widget took the mouse this frame

    // ---- drawing --------------------------------------------------------
    SDL_Renderer* renderer() const { return r_; }
    void fill(Rect rc, Color c);
    void frame(Rect rc, Color c, int t = 1);
    void line(int x1, int y1, int x2, int y2, Color c);
    int text(int x, int y, const std::string& s, Color c = theme::text);   // returns the end x
    int textWidth(const std::string& s) const;
    void textCentered(Rect rc, const std::string& s, Color c = theme::text);
    void textClipped(Rect rc, const std::string& s, Color c = theme::text);  // cut with "…"
    void setClip(const Rect* rc);

    // ---- widgets --------------------------------------------------------
    bool hover(Rect rc) const;          // mouse over rc and not blocked
    bool button(Rect rc, const std::string& label, bool enabled = true, bool selected = false,
                const std::string& tip = "");
    bool checkbox(Rect rc, const std::string& label, bool& v, const std::string& tip = "");
    // returns true when the value changed; filter maps a typed code point to
    // the stored one (0 = reject)
    bool textField(int id, Rect rc, std::string& v, size_t maxChars,
                   const std::function<uint32_t(uint32_t)>& filter, const std::string& tip = "");
    void tooltip(const std::string& s) { tip_ = s; }

    static size_t utf8Length(const std::string& s);
    static std::string utf8PopBack(const std::string& s);

private:
    int glyphIndex(uint32_t cp) const;
    SDL_Renderer* r_ = nullptr;
    SDL_Texture* font_ = nullptr;
    std::string tip_;
    Uint32 lastClick_ = 0;
    int lastClickX_ = 0, lastClickY_ = 0;
    Uint32 caretTime_ = 0;
    bool selectAll_ = false;
};

// decode one code point, advance i
uint32_t utf8Next(const std::string& s, size_t& i);
std::string utf8Encode(uint32_t cp);

}  // namespace editor
