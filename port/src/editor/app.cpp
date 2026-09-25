// Supaplex (Amiga) level editor.
//
// Edits data/LEVELS.DAT of the port (111 levels of 1536 bytes) with the
// game's own tile graphics and the rules of the Amiga version: special port
// records, start camera, border, 10 special ports (docs/06-formats.md,
// docs/09-editor.md). Levels that were not edited are saved byte for byte.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "amiga/gamefiles.hpp"
#include "gfx.hpp"
#include "level.hpp"
#include "process.hpp"
#include "ui.hpp"

namespace fs = std::filesystem;

namespace editor {

namespace {

constexpr int kToolbarH = 32, kStatusH = 22, kLeftW = 236, kRightW = 272, kIssuesH = 112, kRowH = 18;
const int kZooms[] = {8, 12, 16, 20, 24, 32, 40, 48, 64};
constexpr int kZoomCount = int(sizeof kZooms / sizeof kZooms[0]);
constexpr int kCamW = 20, kCamH = 13;  // cells visible in the game's play area (320 x 208)

enum class Tool { Pencil, Line, Rect, FillRect, Flood, Select };

struct ToolInfo { Tool t; const char* name; const char* key; const char* tip; SDL_Keycode sym; };
const ToolInfo kTools[] = {
    {Tool::Pencil, "Карандаш", "P", "Рисовать по клеткам (ЛКМ — основной тайл, ПКМ — второй)", SDLK_p},
    {Tool::Line, "Линия", "L", "Прямая линия", SDLK_l},
    {Tool::Rect, "Рамка", "R", "Контур прямоугольника", SDLK_r},
    {Tool::FillRect, "Прямоуг.", "F", "Залитый прямоугольник", SDLK_f},
    {Tool::Flood, "Заливка", "G", "Залить область одинаковых клеток", SDLK_g},
    {Tool::Select, "Выделение", "S", "Выделить область: Ctrl+C/X/V, Del, Shift+H/V — отразить", SDLK_s},
};

struct Clip {
    int w = 0, h = 0;
    std::vector<uint8_t> t;
    std::vector<SpecialPort> ports;  // relative cells (y * w + x)
};

std::string trimTitle(const std::string& t) {
    size_t a = t.find_first_not_of(" -"), b = t.find_last_not_of(" -");
    if (a == std::string::npos) return "";
    return t.substr(a, b - a + 1);
}

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

// characters the game's font has: upper-case ASCII 32..95
uint32_t titleFilter(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') cp -= 32;
    if (cp >= 0x430 && cp <= 0x44F) return 0;  // no Cyrillic in the game font
    return (cp >= 32 && cp <= 95) ? cp : 0;
}

uint32_t upperAny(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0x430 && cp <= 0x44F) return cp - 32;
    return cp >= 32 ? cp : 0;
}

}  // namespace

class App {
public:
    int run(int argc, char** argv);

private:
    // ---- setup ----
    bool init(int argc, char** argv);
    void loadSettings();
    void saveSettings();
    void updateLayout();

    // ---- frame ----
    void frame();
    void drawToolbar();
    void drawLevelList(Rect r);
    void drawRightPanel(Rect r);
    void drawPalette(Rect r, int& y);
    void drawProperties(Rect r, int& y);
    void drawPorts(Rect r, int& y);
    void drawMinimap(Rect r);
    void drawMap(Rect r);
    void drawIssues(Rect r);
    void drawStatus(Rect r);
    void drawModal();
    void drawHelp();
    void handleKeys();
    void handleMapInput(Rect r);

    // ---- editing ----
    Level& lvl() { return set_.levels[size_t(cur_)]; }
    void beginEdit();
    void endEdit(const char* what = nullptr);
    bool paint(int x, int y, int code);
    void paintLine(int x0, int y0, int x1, int y1, int code);
    void paintRect(int x0, int y0, int x1, int y1, int code, bool filled);
    void flood(int x, int y, int code);
    void syncPorts(Level& l);
    void undo();
    void redo();
    void selectLevel(int i);
    void copySelection(bool cut);
    void pasteAt(int x, int y);
    void mirrorClip(bool horizontal);
    void mirrorSelection(bool horizontal);
    void deleteSelection();
    void swapLevels(int a, int b);
    void replaceLevel(int i, const Level& l, const char* what);

    // ---- files ----
    bool save();
    void reload();
    void exportLevel();
    void exportPicture();
    void dropFile(const std::string& path);
    void startTest();
    void requestQuit();

    // ---- view ----
    void zoomAt(int newZoomIndex, int px, int py);
    void fitMap(bool keepZoom = false);
    void centreOn(int cx, int cy);
    bool cellAt(int px, int py, int& cx, int& cy) const;

    void message(const std::string& s, Color c = theme::text) { status_ = s; statusColor_ = c; statusTime_ = SDL_GetTicks(); }
    void ask(const std::string& title, const std::string& text, std::vector<std::string> buttons,
             std::function<void(int)> done) {
        modal_ = {title, text, std::move(buttons), std::move(done)};
        modalOn_ = true;
    }
    bool anyUnsaved() const { return std::any_of(unsaved_.begin(), unsaved_.end(), [](bool b) { return b; }); }
    const std::vector<Issue>& issues();

    // state
    fs::path gameDir_, dataDir_, levelsPath_;
    amiga::GameFiles files_;
    LevelSet set_;
    TileGfx gfx_;
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* tiles_ = nullptr;
    Ui ui_;
    int uiScale_ = 0;  // 0 = automatic
    float scale_ = 1;  // window pixels per logical pixel
    int vw_ = 1280, vh_ = 800;
    bool running_ = true;

    int cur_ = 0;
    std::array<std::vector<Level>, kLevelCount> undo_, redo_;
    std::array<bool, kLevelCount> unsaved_{};
    Level before_;
    bool editing_ = false;

    Tool tool_ = Tool::Pencil;
    int primary_ = T_BASE, secondary_ = T_SPACE;
    int zoomIdx_ = 3;
    int panX_ = 0, panY_ = 0;
    bool grid_ = true, showCamera_ = true, fitPending_ = true, keepZoom_ = false;

    // mouse on the map
    bool stroking_ = false, panning_ = false;
    int strokeCode_ = 0, sx_ = 0, sy_ = 0, lx_ = -1, ly_ = -1;
    int panStartX_ = 0, panStartY_ = 0, panOrigX_ = 0, panOrigY_ = 0;
    int hoverX_ = -1, hoverY_ = -1;

    bool hasSel_ = false, selecting_ = false;
    int selX0_ = 0, selY0_ = 0, selX1_ = 0, selY1_ = 0;
    Clip clip_;
    bool pasting_ = false;
    bool haveLevelClip_ = false;
    Level levelClip_;

    std::string status_;
    Color statusColor_ = theme::text;
    Uint32 statusTime_ = 0;
    int flashX_ = -1, flashY_ = -1;
    Uint32 flashTime_ = 0;

    struct Modal { std::string title, text; std::vector<std::string> buttons; std::function<void(int)> done; } modal_;
    bool modalOn_ = false;
    bool help_ = false, helpOpened_ = false;

    std::string filter_;
    int listScroll_ = 0, portScroll_ = 0;
    bool listFollow_ = true;
    std::string titleEdit_;
    int titleFor_ = -1;

    std::vector<Issue> issues_;
    std::vector<uint64_t> issueStamp_;
    uint64_t changeCounter_ = 1, issuesAt_ = 0;
    int issuesLevel_ = -1;
    int issueScroll_ = 0;

    // level test: the game runs in a worker thread (SDL threads work with
    // every compiler, std::thread does not with MinGW's win32 thread model)
    bool testing_ = false;
    SDL_Thread* testThread_ = nullptr;
    SDL_atomic_t testDone_{};
    int testExit_ = 0;
    std::string testExe_;
    std::vector<std::string> testArgs_;
    static int testThreadMain(void* self);
    void finishTest();
    bool backupDone_ = false;

    // testing without a screen: --script FILE (see runScript)
    struct ScriptLine { long frame; std::string cmd; std::vector<std::string> a; };
    std::vector<ScriptLine> script_;
    long frameNo_ = 0;
    void runScript();
    void screenshot(const std::string& path);
    std::string pendingShot_;
};

// ---------------------------------------------------------------------------
// setup

bool App::init(int argc, char** argv) {
    std::string dataArg;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dataArg = argv[++i];
        else if (a == "--scale" && i + 1 < argc) uiScale_ = std::max(1, std::atoi(argv[++i]));
        else if (a == "--script" && i + 1 < argc) {
            std::ifstream f(argv[++i]);
            std::string line;
            while (std::getline(f, line)) {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream ls(line);
                ScriptLine sl;
                ls >> sl.frame >> sl.cmd;
                std::string w;
                while (ls >> w) sl.a.push_back(w);
                script_.push_back(sl);
            }
        }
        else if (a == "--help" || a == "-h") {
            std::printf("usage: supaplex_editor [--data DIR] [--scale 1|2]\n"
                        "Edits DIR/LEVELS.DAT (default: data\\ next to the program).\n");
            return false;
        }
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return false; }
    char* base = SDL_GetBasePath();
    gameDir_ = fs::u8path(base ? base : "");
    if (base) SDL_free(base);
    dataDir_ = dataArg.empty() ? gameDir_ / "data" : fs::u8path(dataArg);
    levelsPath_ = dataDir_ / "LEVELS.DAT";

    auto fail = [&](const std::string& m) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Редактор уровней Supaplex", m.c_str(), nullptr);
        std::fprintf(stderr, "%s\n", m.c_str());
        return false;
    };
    if (!files_.openDir(dataDir_.u8string()))
        return fail("Не найдены данные игры в папке\n" + dataDir_.u8string() + "\n\n" + files_.error() +
                    "\n\nЗапустите один раз игру (supaplex) — она создаст папку data из образа дискеты.");
    std::string err;
    if (!set_.load(levelsPath_.u8string(), &err)) return fail(err);
    if (!gfx_.load(files_, &err)) return fail(err);

    loadSettings();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    win_ = SDL_CreateWindow("Редактор уровней Supaplex (Amiga)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1400, 860,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win_) return fail(SDL_GetError());
    SDL_SetWindowMinimumSize(win_, 1000, 640);
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
    if (!ren_) return fail(SDL_GetError());
    if (!ui_.init(ren_)) return fail("font texture");
    tiles_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, gfx_.atlasWidth(), kTilePx);
    SDL_UpdateTexture(tiles_, nullptr, gfx_.atlas().data(), gfx_.atlasWidth() * 4);
    SDL_SetTextureBlendMode(tiles_, SDL_BLENDMODE_BLEND);
    SDL_StopTextInput();
    updateLayout();
    message("Загружено: " + levelsPath_.u8string(), theme::ok);
    return true;
}

void App::loadSettings() {
    std::ifstream f(gameDir_ / "editor.ini");
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        int v = std::atoi(line.c_str() + eq + 1);
        if (k == "level") cur_ = std::clamp(v, 0, kLevelCount - 1);
        else if (k == "zoom") { zoomIdx_ = std::clamp(v, 0, kZoomCount - 1); keepZoom_ = true; }
        else if (k == "grid") grid_ = v != 0;
        else if (k == "camera") showCamera_ = v != 0;
        else if (k == "scale" && uiScale_ == 0) uiScale_ = std::clamp(v, 0, 3);
        else if (k == "primary") primary_ = std::clamp(v, 0, T_COUNT - 1);
        else if (k == "secondary") secondary_ = std::clamp(v, 0, T_COUNT - 1);
    }
}

void App::saveSettings() {
    std::ofstream f(gameDir_ / "editor.ini");
    f << "level=" << cur_ << "\nzoom=" << zoomIdx_ << "\ngrid=" << grid_ << "\ncamera=" << showCamera_
      << "\nscale=" << uiScale_ << "\nprimary=" << primary_ << "\nsecondary=" << secondary_ << "\n";
}

void App::updateLayout() {
    int ww, wh, ow, oh;
    SDL_GetWindowSize(win_, &ww, &wh);
    SDL_GetRendererOutputSize(ren_, &ow, &oh);
    float pixelRatio = ww > 0 ? float(ow) / float(ww) : 1.f;
    int s = uiScale_ ? uiScale_ : (oh >= 1500 ? 2 : 1);
    scale_ = float(s);  // mouse coordinates are in window points
    SDL_RenderSetScale(ren_, pixelRatio * float(s), pixelRatio * float(s));
    vw_ = int(ww / scale_);
    vh_ = int(wh / scale_);
}

// ---------------------------------------------------------------------------
// editing

void App::beginEdit() {
    before_ = lvl();
    editing_ = true;
}

void App::endEdit(const char* what) {
    if (!editing_) return;
    editing_ = false;
    syncPorts(lvl());
    if (lvl().b == before_.b) return;
    auto& u = undo_[size_t(cur_)];
    u.push_back(before_);
    if (u.size() > 1000) u.erase(u.begin());
    redo_[size_t(cur_)].clear();
    set_.edited[size_t(cur_)] = true;
    unsaved_[size_t(cur_)] = true;
    changeCounter_++;
    if (what) message(what);
}

// special port records follow the special port tiles: a new tile gets a
// record, a removed tile loses it
void App::syncPorts(Level& l) {
    auto ports = l.specialPorts();
    std::vector<SpecialPort> keep;
    for (const SpecialPort& p : ports)
        if (p.cell < kMapW * kMapH && isSpecialPort(l.b[size_t(p.cell)])) keep.push_back(p);
    for (int i = 0; i < kMapW * kMapH; i++)
        if (isSpecialPort(l.b[size_t(i)]) &&
            std::none_of(keep.begin(), keep.end(), [&](const SpecialPort& p) { return p.cell == i; })) {
            SpecialPort p;
            p.cell = i;
            keep.push_back(p);
        }
    bool changed = keep.size() != ports.size();
    for (size_t i = 0; !changed && i < keep.size(); i++) changed = keep[i].cell != ports[i].cell;
    if (changed) l.setSpecialPorts(keep);
}

bool App::paint(int x, int y, int code) {
    Level& l = lvl();
    if (!l.inside(x, y) || l.tile(x, y) == code) return false;
    if (isSpecialPort(code) && !isSpecialPort(l.tile(x, y)) && l.count(T_SPORT_RIGHT) + l.count(T_SPORT_DOWN) +
                                                                          l.count(T_SPORT_LEFT) + l.count(T_SPORT_UP) >=
                                                                      kMaxSpecialPorts) {
        message("Больше 10 особых портов в формате уровня не помещается", theme::error);
        return false;
    }
    l.setTile(x, y, code);
    return true;
}

void App::paintLine(int x0, int y0, int x1, int y1, int code) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        paint(x0, y0, code);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

void App::paintRect(int x0, int y0, int x1, int y1, int code, bool filled) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (filled || x == x0 || x == x1 || y == y0 || y == y1) paint(x, y, code);
}

void App::flood(int x, int y, int code) {
    Level& l = lvl();
    int from = l.tile(x, y);
    if (from == code) return;
    std::vector<std::pair<int, int>> st{{x, y}};
    while (!st.empty()) {
        auto [cx, cy] = st.back();
        st.pop_back();
        if (!l.inside(cx, cy) || l.tile(cx, cy) != from) continue;
        if (!paint(cx, cy, code)) continue;
        st.push_back({cx + 1, cy});
        st.push_back({cx - 1, cy});
        st.push_back({cx, cy + 1});
        st.push_back({cx, cy - 1});
    }
}

void App::undo() {
    auto& u = undo_[size_t(cur_)];
    if (u.empty()) { message("Нечего отменять"); return; }
    redo_[size_t(cur_)].push_back(lvl());
    lvl() = u.back();
    u.pop_back();
    set_.edited[size_t(cur_)] = true;
    unsaved_[size_t(cur_)] = true;
    changeCounter_++;
    message("Отменено");
}

void App::redo() {
    auto& r = redo_[size_t(cur_)];
    if (r.empty()) { message("Нечего возвращать"); return; }
    undo_[size_t(cur_)].push_back(lvl());
    lvl() = r.back();
    r.pop_back();
    set_.edited[size_t(cur_)] = true;
    unsaved_[size_t(cur_)] = true;
    changeCounter_++;
    message("Возвращено");
}

void App::selectLevel(int i) {
    i = std::clamp(i, 0, kLevelCount - 1);
    if (i == cur_) return;
    if (stroking_) { endEdit(); stroking_ = false; }
    cur_ = i;
    hasSel_ = selecting_ = pasting_ = false;
    listFollow_ = true;
    ui_.focus = 0;
}

void App::copySelection(bool cut) {
    if (!hasSel_) { message("Сначала выделите область (инструмент «Выделение», S)"); return; }
    int x0 = std::min(selX0_, selX1_), x1 = std::max(selX0_, selX1_), y0 = std::min(selY0_, selY1_), y1 = std::max(selY0_, selY1_);
    clip_.w = x1 - x0 + 1;
    clip_.h = y1 - y0 + 1;
    clip_.t.clear();
    clip_.ports.clear();
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            clip_.t.push_back(uint8_t(lvl().tile(x, y)));
            int k = lvl().findPort(y * kMapW + x);
            if (k >= 0) {
                SpecialPort p = lvl().specialPorts()[size_t(k)];
                p.cell = (y - y0) * clip_.w + (x - x0);
                clip_.ports.push_back(p);
            }
        }
    if (cut) {
        beginEdit();
        paintRect(x0, y0, x1, y1, secondary_, true);
        endEdit("Вырезано");
    } else {
        message(fmt("Скопировано %d × %d", clip_.w, clip_.h));
    }
}

void App::pasteAt(int x, int y) {
    if (clip_.w == 0) return;
    beginEdit();
    for (int j = 0; j < clip_.h; j++)
        for (int i = 0; i < clip_.w; i++) paint(x + i, y + j, clip_.t[size_t(j * clip_.w + i)]);
    syncPorts(lvl());
    // carry the special port settings along
    auto ports = lvl().specialPorts();
    for (const SpecialPort& cp : clip_.ports) {
        int cell = (y + cp.cell / clip_.w) * kMapW + (x + cp.cell % clip_.w);
        for (SpecialPort& p : ports)
            if (p.cell == cell) { p.gravity = cp.gravity; p.freezeZonks = cp.freezeZonks; p.freezeEnemies = cp.freezeEnemies; }
    }
    lvl().setSpecialPorts(ports);
    endEdit("Вставлено");
}

void App::mirrorClip(bool horizontal) {
    if (clip_.w == 0) return;
    Clip c = clip_;
    for (int y = 0; y < c.h; y++)
        for (int x = 0; x < c.w; x++) {
            int sx = horizontal ? c.w - 1 - x : x, sy = horizontal ? y : c.h - 1 - y;
            int t = clip_.t[size_t(sy * c.w + sx)];
            c.t[size_t(y * c.w + x)] = uint8_t(horizontal ? mirrorTileH(t) : mirrorTileV(t));
        }
    for (SpecialPort& p : c.ports) {
        int px = p.cell % c.w, py = p.cell / c.w;
        if (horizontal) px = c.w - 1 - px; else py = c.h - 1 - py;
        p.cell = py * c.w + px;
    }
    clip_ = c;
}

void App::mirrorSelection(bool horizontal) {
    if (!hasSel_) { message("Сначала выделите область"); return; }
    copySelection(false);
    mirrorClip(horizontal);
    pasteAt(std::min(selX0_, selX1_), std::min(selY0_, selY1_));
    message(horizontal ? "Отражено слева направо" : "Отражено сверху вниз");
}

void App::deleteSelection() {
    if (!hasSel_) return;
    beginEdit();
    paintRect(selX0_, selY0_, selX1_, selY1_, secondary_, true);
    endEdit("Очищено");
}

void App::swapLevels(int a, int b) {
    if (a < 0 || b < 0 || a >= kLevelCount || b >= kLevelCount) return;
    std::swap(set_.levels[size_t(a)], set_.levels[size_t(b)]);
    std::swap(undo_[size_t(a)], undo_[size_t(b)]);
    std::swap(redo_[size_t(a)], redo_[size_t(b)]);
    std::swap(set_.edited[size_t(a)], set_.edited[size_t(b)]);
    unsaved_[size_t(a)] = unsaved_[size_t(b)] = true;
    changeCounter_++;
    cur_ = b;
    listFollow_ = true;
    message(fmt("Уровень перемещён: %03d → %03d", a + 1, b + 1));
}

void App::replaceLevel(int i, const Level& l, const char* what) {
    int old = cur_;
    cur_ = i;
    beginEdit();
    lvl() = l;
    endEdit(what);
    set_.edited[size_t(i)] = true;
    unsaved_[size_t(i)] = true;
    cur_ = old;
}

const std::vector<Issue>& App::issues() {
    if (issuesLevel_ != cur_ || issuesAt_ != changeCounter_) {
        issues_ = lvl().validate();
        issuesLevel_ = cur_;
        issuesAt_ = changeCounter_;
    }
    return issues_;
}

// ---------------------------------------------------------------------------
// files

bool App::save() {
    std::error_code ec;
    if (!backupDone_) {
        fs::copy_file(levelsPath_, fs::path(levelsPath_.u8string() + ".bak"), fs::copy_options::overwrite_existing, ec);
        backupDone_ = true;
    }
    std::string err;
    if (!set_.save(levelsPath_.u8string(), &err)) { message("Ошибка сохранения: " + err, theme::error); return false; }
    unsaved_.fill(false);
    changeCounter_++;
    message("Сохранено: " + levelsPath_.u8string() + " (копия прежнего файла — LEVELS.DAT.bak)", theme::ok);
    return true;
}

void App::reload() {
    std::string err;
    LevelSet s;
    if (!s.load(levelsPath_.u8string(), &err)) { message(err, theme::error); return; }
    set_ = s;
    for (auto& u : undo_) u.clear();
    for (auto& r : redo_) r.clear();
    unsaved_.fill(false);
    changeCounter_++;
    message("Уровни перечитаны с диска", theme::ok);
}

void App::exportLevel() {
    fs::path dir = gameDir_ / "export";
    std::error_code ec;
    fs::create_directories(dir, ec);
    Level l = lvl();
    l.normalize();
    fs::path p = dir / fmt("LEVEL%03d.SP", cur_ + 1);
    if (LevelSet::writeFile(p.u8string(), std::vector<uint8_t>(l.b.begin(), l.b.end())))
        message("Уровень сохранён в " + p.u8string(), theme::ok);
    else
        message("Не удалось записать " + p.u8string(), theme::error);
}

void App::exportPicture() {
    fs::path dir = gameDir_ / "export";
    std::error_code ec;
    fs::create_directories(dir, ec);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, kMapW * kTilePx, kMapH * kTilePx, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    const auto& a = gfx_.atlas();
    for (int cy = 0; cy < kMapH; cy++)
        for (int cx = 0; cx < kMapW; cx++) {
            int img = TileGfx::imageFor(lvl().tile(cx, cy));
            for (int y = 0; y < kTilePx; y++)
                std::memcpy(static_cast<uint8_t*>(s->pixels) + (cy * kTilePx + y) * s->pitch + cx * kTilePx * 4,
                            &a[size_t(y * gfx_.atlasWidth() + img * kTilePx)], kTilePx * 4);
        }
    fs::path p = dir / fmt("LEVEL%03d.bmp", cur_ + 1);
    if (SDL_SaveBMP(s, p.u8string().c_str()) == 0) message("Картинка уровня: " + p.u8string(), theme::ok);
    else message("Не удалось записать " + p.u8string(), theme::error);
    SDL_FreeSurface(s);
}

void App::dropFile(const std::string& path) {
    std::vector<uint8_t> d;
    if (!LevelSet::readFile(path, d)) { message("Не удалось прочитать " + path, theme::error); return; }
    std::string name = fs::u8path(path).filename().u8string();
    if (d.size() == size_t(kLevelCount * kLevelSize)) {
        ask("Импорт набора уровней", "Заменить все 111 уровней уровнями из файла\n" + name +
            "?\n\nКамера и особые порты будут приведены к правилам Amiga при сохранении.\nОтменить можно по каждому уровню (Ctrl+Z).",
            {"Заменить все", "Отмена"}, [this, d](int b) {
                if (b != 0) return;
                for (int i = 0; i < kLevelCount; i++) {
                    Level l;
                    std::copy_n(d.begin() + std::ptrdiff_t(i * kLevelSize), kLevelSize, l.b.begin());
                    replaceLevel(i, l, nullptr);
                }
                message("Импортированы 111 уровней (не забудьте сохранить)", theme::ok);
            });
    } else if (d.size() >= size_t(kLevelSize)) {
        Level l;
        std::copy_n(d.begin(), kLevelSize, l.b.begin());
        ask("Импорт уровня", "Загрузить уровень из файла\n" + name + "\nна место уровня " + fmt("%03d", cur_ + 1) + "?",
            {"Загрузить", "Отмена"}, [this, l](int b) {
                if (b == 0) replaceLevel(cur_, l, "Уровень импортирован (Ctrl+Z — отменить)");
            });
    } else {
        message("Это не файл уровня: " + name, theme::error);
    }
}

void App::startTest() {
    if (testing_) return;
    const auto& is = issues();
    for (const Issue& i : is)
        if (i.level == Issue::Error && lvl().count(T_MURPHY) == 0) { message("Без Murphy уровень не запустить", theme::error); return; }
#ifdef _WIN32
    fs::path exe = gameDir_ / "supaplex.exe";
#else
    fs::path exe = gameDir_ / "supaplex";
#endif
    std::error_code ec;
    if (!fs::exists(exe, ec)) { message("Не найдена игра: " + exe.u8string(), theme::error); return; }
    // a copy of the data folder with the levels as they are now
    fs::path dir = gameDir_ / "editor_test";
    fs::create_directories(dir, ec);
    for (const char* f : {"INTRO.BIN", "MAIN.BIN", "GRAPHICS.BIN", "HISCORE.BIN"})
        fs::copy_file(dataDir_ / f, dir / f, fs::copy_options::overwrite_existing, ec);
    std::vector<uint8_t> all;
    for (int i = 0; i < kLevelCount; i++) {
        Level l = set_.levels[size_t(i)];
        if (set_.edited[size_t(i)]) l.normalize();
        all.insert(all.end(), l.b.begin(), l.b.end());
    }
    if (!LevelSet::writeFile((dir / "LEVELS.DAT").u8string(), all)) { message("Не удалось подготовить тест", theme::error); return; }
    fs::remove(dir / "hiscores.sav", ec);
    int level = cur_ + 1;
    testExe_ = exe.u8string();
    testArgs_ = {"--data", dir.u8string(), "--save", (dir / "hiscores.sav").u8string(), "--test-level", std::to_string(level)};
    SDL_AtomicSet(&testDone_, 0);
    testThread_ = SDL_CreateThread(testThreadMain, "level test", this);
    if (!testThread_) { message("Не удалось запустить игру", theme::error); return; }
    testing_ = true;
    message(fmt("Игра запущена на уровне %03d — после уровня она закроется сама (Esc — выйти сразу)", level), theme::info);
}

int App::testThreadMain(void* self) {
    App* a = static_cast<App*>(self);
    a->testExit_ = runProcess(a->testExe_, a->testArgs_);
    SDL_AtomicSet(&a->testDone_, 1);
    return 0;
}

void App::finishTest() {
    if (!testing_ || !SDL_AtomicGet(&testDone_)) return;
    SDL_WaitThread(testThread_, nullptr);
    testThread_ = nullptr;
    testing_ = false;
    int rc = testExit_;
    message(rc == 0 ? "Тест окончен" : rc < 0 ? "Не удалось запустить игру" : fmt("Игра завершилась с кодом %d", rc),
            rc == 0 ? theme::ok : theme::error);
    SDL_RaiseWindow(win_);
}

void App::requestQuit() {
    if (!anyUnsaved()) { running_ = false; return; }
    ask("Есть несохранённые изменения", "Сохранить изменения в LEVELS.DAT перед выходом?",
        {"Сохранить", "Не сохранять", "Отмена"}, [this](int b) {
            if (b == 0 && save()) running_ = false;
            if (b == 1) running_ = false;
        });
}

// Script lines: "FRAME move X Y", "FRAME down X Y [l|m|r]", "FRAME up X Y [l|m|r]",
// "FRAME click X Y [l|m|r]", "FRAME key NAME [ctrl] [shift] [alt]", "FRAME text STRING",
// "FRAME wheel N [ctrl]", "FRAME shot FILE.bmp", "FRAME quit". Coordinates are logical.
void App::runScript() {
    for (const ScriptLine& l : script_) {
        if (l.frame != frameNo_) continue;
        auto num = [&](size_t i) { return i < l.a.size() ? std::atoi(l.a[i].c_str()) : 0; };
        auto has = [&](const char* w) { return std::find(l.a.begin(), l.a.end(), w) != l.a.end(); };
        auto button = [&]() -> Uint8 {
            std::string b = l.a.size() > 2 ? l.a[2] : "l";
            return b == "r" ? SDL_BUTTON_RIGHT : b == "m" ? SDL_BUTTON_MIDDLE : SDL_BUTTON_LEFT;
        };
        SDL_Event e{};
        auto mouse = [&](Uint32 type) {
            e = SDL_Event{};
            e.type = type;
            e.button.button = button();
            e.button.x = int(num(0) * scale_);
            e.button.y = int(num(1) * scale_);
            SDL_PushEvent(&e);
        };
        if (l.cmd == "move") {
            e.type = SDL_MOUSEMOTION;
            e.motion.x = int(num(0) * scale_);
            e.motion.y = int(num(1) * scale_);
            SDL_WarpMouseInWindow(win_, e.motion.x, e.motion.y);
            SDL_PushEvent(&e);
        } else if (l.cmd == "down") mouse(SDL_MOUSEBUTTONDOWN);
        else if (l.cmd == "up") mouse(SDL_MOUSEBUTTONUP);
        else if (l.cmd == "click") { mouse(SDL_MOUSEBUTTONDOWN); mouse(SDL_MOUSEBUTTONUP); }
        else if (l.cmd == "key") {
            SDL_Keycode k = SDL_GetKeyFromName(l.a.empty() ? "" : l.a[0].c_str());
            Uint16 mod = Uint16((has("ctrl") ? KMOD_LCTRL : 0) | (has("shift") ? KMOD_LSHIFT : 0) | (has("alt") ? KMOD_LALT : 0));
            e.type = SDL_KEYDOWN;
            e.key.keysym.sym = k;
            e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
            e.key.keysym.mod = mod;
            SDL_PushEvent(&e);
        } else if (l.cmd == "text") {
            e.type = SDL_TEXTINPUT;
            std::string t = l.a.empty() ? "" : l.a[0];
            std::snprintf(e.text.text, sizeof e.text.text, "%s", t.c_str());
            SDL_PushEvent(&e);
        } else if (l.cmd == "wheel") {
            e.type = SDL_MOUSEWHEEL;
            e.wheel.y = num(0);
            SDL_PushEvent(&e);
        } else if (l.cmd == "shot") {
            pendingShot_ = l.a.empty() ? "shot.bmp" : l.a[0];
        } else if (l.cmd == "drop") {
            e.type = SDL_DROPFILE;
            e.drop.file = SDL_strdup(l.a.empty() ? "" : l.a[0].c_str());
            SDL_PushEvent(&e);
        } else if (l.cmd == "save") {
            save();
        } else if (l.cmd == "quit") {
            running_ = false;
        }
    }
}

void App::screenshot(const std::string& path) {
    int w, h;
    SDL_GetRendererOutputSize(ren_, &w, &h);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path.c_str());
    SDL_FreeSurface(s);
}

// ---------------------------------------------------------------------------
// view

bool App::cellAt(int px, int py, int& cx, int& cy) const {
    int z = kZooms[zoomIdx_];
    int mx = px + panX_, my = py + panY_;
    if (mx < 0 || my < 0) return false;
    cx = mx / z;
    cy = my / z;
    return cx < kMapW && cy < kMapH;
}

void App::zoomAt(int idx, int px, int py) {
    idx = std::clamp(idx, 0, kZoomCount - 1);
    if (idx == zoomIdx_) return;
    float oz = float(kZooms[zoomIdx_]), nz = float(kZooms[idx]);
    float wx = (px + panX_) / oz, wy = (py + panY_) / oz;
    zoomIdx_ = idx;
    panX_ = int(wx * nz) - px;
    panY_ = int(wy * nz) - py;
}

void App::fitMap(bool keepZoom) {
    int mapW = vw_ - kLeftW - kRightW, mapH = vh_ - kToolbarH - kStatusH - kIssuesH;
    int best = 0;
    for (int i = 0; i < kZoomCount; i++)
        if (kZooms[i] * kMapW <= mapW - 8 && kZooms[i] * kMapH <= mapH - 8) best = i;
    if (!keepZoom) zoomIdx_ = best;
    int z = kZooms[zoomIdx_];
    panX_ = -(mapW - z * kMapW) / 2;
    panY_ = -(mapH - z * kMapH) / 2;
}

void App::centreOn(int cx, int cy) {
    int mapW = vw_ - kLeftW - kRightW, mapH = vh_ - kToolbarH - kStatusH - kIssuesH;
    int z = kZooms[zoomIdx_];
    panX_ = cx * z + z / 2 - mapW / 2;
    panY_ = cy * z + z / 2 - mapH / 2;
    flashX_ = cx;
    flashY_ = cy;
    flashTime_ = SDL_GetTicks();
}

// ---------------------------------------------------------------------------
// frame

int App::run(int argc, char** argv) {
    if (!init(argc, argv)) return 1;
    while (running_) {
        ui_.beginFrame();
        runScript();
        frameNo_++;
        SDL_Event e;
        bool gotEvent = SDL_WaitEventTimeout(&e, testing_ ? 100 : 16);
        while (gotEvent) {
            if (e.type == SDL_QUIT) requestQuit();
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) updateLayout();
            else if (e.type == SDL_DROPFILE) {
                dropFile(e.drop.file);
                SDL_free(e.drop.file);
            }
            ui_.handleEvent(e, scale_);
            gotEvent = SDL_PollEvent(&e);
        }
        finishTest();
        frame();
    }
    if (testThread_) SDL_WaitThread(testThread_, nullptr);
    saveSettings();
    std::error_code ec;
    fs::remove_all(gameDir_ / "editor_test", ec);
    if (tiles_) SDL_DestroyTexture(tiles_);
    ui_.destroy();
    SDL_DestroyRenderer(ren_);
    SDL_DestroyWindow(win_);
    SDL_Quit();
    return 0;
}

void App::frame() {
    if (fitPending_) { fitMap(keepZoom_); fitPending_ = false; }
    ui_.modal = modalOn_ || help_;
    if (ui_.focus != 2) {  // the title field shows the level unless it is being edited
        titleEdit_ = lvl().title();
        titleEdit_.erase(titleEdit_.find_last_not_of(' ') + 1);
    }

    std::string t = fmt("Редактор уровней Supaplex (Amiga) — %03d %s%s", cur_ + 1, trimTitle(lvl().title()).c_str(),
                        anyUnsaved() ? " *" : "");
    SDL_SetWindowTitle(win_, t.c_str());

    SDL_SetRenderDrawColor(ren_, theme::bg.r, theme::bg.g, theme::bg.b, 255);
    SDL_RenderClear(ren_);

    Rect mapR{kLeftW, kToolbarH, vw_ - kLeftW - kRightW, vh_ - kToolbarH - kStatusH - kIssuesH};
    drawMap(mapR);
    drawToolbar();
    drawLevelList({0, kToolbarH, kLeftW, vh_ - kToolbarH - kStatusH});
    drawRightPanel({vw_ - kRightW, kToolbarH, kRightW, vh_ - kToolbarH - kStatusH});
    drawIssues({kLeftW, vh_ - kStatusH - kIssuesH, vw_ - kLeftW - kRightW, kIssuesH});
    drawStatus({0, vh_ - kStatusH, vw_, kStatusH});
    if (!ui_.modal) {
        handleMapInput(mapR);
        handleKeys();
    }
    if (help_) drawHelp();
    if (modalOn_) drawModal();
    ui_.endFrame();
    if (!pendingShot_.empty()) { screenshot(pendingShot_); pendingShot_.clear(); }
    SDL_RenderPresent(ren_);
}

void App::drawToolbar() {
    Rect bar{0, 0, vw_, kToolbarH};
    ui_.fill(bar, theme::panel);
    ui_.line(0, kToolbarH - 1, vw_, kToolbarH - 1, theme::line);
    int x = 6, y = 4, h = kToolbarH - 8;
    auto btn = [&](const std::string& label, int w, bool enabled, bool sel, const std::string& tip) {
        bool r = ui_.button({x, y, w, h}, label, enabled, sel, tip);
        x += w + 4;
        return r;
    };
    auto gap = [&] { x += 8; };
    if (btn("Сохранить", 92, anyUnsaved(), false, "Записать data\\LEVELS.DAT (Ctrl+S)")) save();
    if (btn("↶", 28, !undo_[size_t(cur_)].empty(), false, "Отменить (Ctrl+Z)")) undo();
    if (btn("↷", 28, !redo_[size_t(cur_)].empty(), false, "Вернуть (Ctrl+Y)")) redo();
    gap();
    for (const ToolInfo& t : kTools) {
        std::string label = std::string(t.name);
        if (btn(label, ui_.textWidth(label) + 16, true, tool_ == t.t, std::string(t.tip) + " (" + t.key + ")")) {
            tool_ = t.t;
            pasting_ = false;
        }
    }
    gap();
    if (btn("Сетка", 60, true, grid_, "Показать сетку (Ctrl+G)")) grid_ = !grid_;
    if (btn("Камера", 68, true, showCamera_, "Показать стартовый экран игры (C)")) showCamera_ = !showCamera_;
    gap();
    if (btn("−", 26, zoomIdx_ > 0, false, "Уменьшить (Ctrl+колесо, Ctrl+−)")) zoomAt(zoomIdx_ - 1, (vw_ - kLeftW - kRightW) / 2, 200);
    ui_.fill({x, y, 50, h}, theme::panel2);
    ui_.textCentered({x, y, 50, h}, fmt("%d px", kZooms[zoomIdx_]));
    x += 54;
    if (btn("+", 26, zoomIdx_ < kZoomCount - 1, false, "Увеличить (Ctrl+колесо, Ctrl+=)"))
        zoomAt(zoomIdx_ + 1, (vw_ - kLeftW - kRightW) / 2, 200);
    if (btn("Весь", 50, true, false, "Показать весь уровень (Ctrl+0)")) fitMap();
    gap();
    if (btn(testing_ ? "Идёт тест…" : "▶ Играть", 100, !testing_, false, "Сыграть этот уровень в игре (F5)")) startTest();
    int right = vw_ - 6;
    if (ui_.button({right - 32, y, 32, h}, "?", true, help_, "Справка (F1)")) { help_ = true; helpOpened_ = true; }
}

void App::drawLevelList(Rect r) {
    ui_.fill(r, theme::panel);
    ui_.line(r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h, theme::line);
    int y = r.y + 6;
    ui_.text(r.x + 8, y, "Уровни", theme::dim);
    y += 20;
    ui_.textField(1, {r.x + 8, y, r.w - 16, 22}, filter_, 23, upperAny, "Поиск по номеру или названию");
    if (filter_.empty() && ui_.focus != 1) ui_.text(r.x + 14, y + 3, "поиск…", theme::dim);
    y += 28;
    int btnH = 24;
    Rect list{r.x + 4, y, r.w - 8, r.h - (y - r.y) - 2 * (btnH + 6) - 8};
    // filtered list
    std::vector<int> idx;
    for (int i = 0; i < kLevelCount; i++) {
        if (!filter_.empty()) {
            std::string hay = fmt("%03d ", i + 1) + set_.levels[size_t(i)].title();
            if (hay.find(filter_) == std::string::npos) continue;
        }
        idx.push_back(i);
    }
    int visible = list.h / kRowH;
    int maxScroll = std::max(0, int(idx.size()) - visible);
    if (listFollow_) {
        auto it = std::find(idx.begin(), idx.end(), cur_);
        if (it != idx.end()) {
            int p = int(it - idx.begin());
            if (p < listScroll_) listScroll_ = p;
            if (p >= listScroll_ + visible) listScroll_ = p - visible + 1;
        }
        listFollow_ = false;
    }
    if (ui_.hover(list) && ui_.wheelY) listScroll_ -= ui_.wheelY * 3;
    listScroll_ = std::clamp(listScroll_, 0, maxScroll);
    ui_.setClip(&list);
    for (int k = 0; k < visible + 1 && listScroll_ + k < int(idx.size()); k++) {
        int i = idx[size_t(listScroll_ + k)];
        Rect row{list.x, list.y + k * kRowH, list.w, kRowH};
        bool sel = i == cur_;
        if (sel) ui_.fill(row, theme::accentDim);
        else if (ui_.hover(row)) ui_.fill(row, theme::panel2);
        // markers: unsaved, errors
        const Level& l = set_.levels[size_t(i)];
        std::string num = fmt("%03d", i + 1);
        ui_.text(row.x + 4, row.y + 1, num, sel ? theme::text : theme::dim);
        ui_.textClipped({row.x + 36, row.y, row.w - 56, row.h}, trimTitle(l.title()));
        if (unsaved_[size_t(i)]) ui_.text(row.x + row.w - 16, row.y + 1, "*", theme::warning);
        if (ui_.hover(row) && ui_.pressed[0]) { selectLevel(i); ui_.captured = true; }
    }
    ui_.setClip(nullptr);
    if (maxScroll > 0) {
        int barH = std::max(20, list.h * visible / int(idx.size()));
        int barY = list.y + (list.h - barH) * listScroll_ / maxScroll;
        ui_.fill({list.x + list.w - 3, barY, 3, barH}, theme::line);
    }
    // level operations
    int by = r.y + r.h - 2 * (btnH + 6) - 2;
    int bw = (r.w - 8 * 2 - 4 * 2) / 3;
    int bx = r.x + 8;
    if (ui_.button({bx, by, bw, btnH}, "Новый", true, false, "Заменить уровень чистым: рамка, Murphy, выход")) {
        ask("Новый уровень", fmt("Заменить уровень %03d чистым уровнем?\n(Ctrl+Z — отменить)", cur_ + 1), {"Заменить", "Отмена"},
            [this](int b) { if (b == 0) replaceLevel(cur_, Level::blank(), "Создан чистый уровень"); });
    }
    if (ui_.button({bx + bw + 4, by, bw, btnH}, "Копия", true, false, "Запомнить весь уровень (Ctrl+Shift+C)")) {
        levelClip_ = lvl(); haveLevelClip_ = true; message(fmt("Уровень %03d скопирован", cur_ + 1));
    }
    if (ui_.button({bx + 2 * (bw + 4), by, bw, btnH}, "Вставка", haveLevelClip_, false,
                   "Заменить уровень скопированным (Ctrl+Shift+V)"))
        replaceLevel(cur_, levelClip_, "Уровень вставлен (Ctrl+Z — отменить)");
    by += btnH + 6;
    if (ui_.button({bx, by, bw, btnH}, "▲ Выше", cur_ > 0, false, "Поменять местами с предыдущим (Alt+↑)")) swapLevels(cur_, cur_ - 1);
    if (ui_.button({bx + bw + 4, by, bw, btnH}, "▼ Ниже", cur_ < kLevelCount - 1, false, "Поменять местами со следующим (Alt+↓)"))
        swapLevels(cur_, cur_ + 1);
    if (ui_.button({bx + 2 * (bw + 4), by, bw, btnH}, "Экспорт", true, false,
                   "Записать уровень в export\\LEVELnnn.SP (Ctrl+E); картинка — Ctrl+P"))
        exportLevel();
}

void App::drawRightPanel(Rect r) {
    ui_.fill(r, theme::panel);
    ui_.line(r.x, r.y, r.x, r.y + r.h, theme::line);
    int y = r.y + 6;
    drawPalette(r, y);
    drawProperties(r, y);
    drawPorts({r.x, y, r.w, r.y + r.h - y - 118}, y);
    drawMinimap({r.x + 8, r.y + r.h - 110, r.w - 16, 104});
}

void App::drawPalette(Rect r, int& y) {
    ui_.text(r.x + 8, y, "Тайлы", theme::dim);
    ui_.text(r.x + 70, y, "ЛКМ", theme::accent);
    ui_.text(r.x + 150, y, "ПКМ", theme::warning);
    y += 20;
    // order: main objects first, decorative variants last
    static const int order[] = {0, 2, 1, 4, 3, 7, 6, 5, 8, 18, 20, 19, 17, 24, 25, 9, 10, 11, 12, 21, 22, 23, 13, 14, 15, 16,
                                26, 27, 38, 39, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37};
    int cell = 36, cols = (r.w - 12) / cell;
    for (int k = 0; k < T_COUNT; k++) {
        int code = order[k];
        Rect c{r.x + 8 + (k % cols) * cell, y + (k / cols) * cell, cell - 2, cell - 2};
        SDL_Rect src{TileGfx::imageFor(code) * kTilePx, 0, kTilePx, kTilePx}, dst{c.x + 1, c.y + 1, 32, 32};
        SDL_RenderCopy(ren_, tiles_, &src, &dst);
        if (isSpecialPort(code)) ui_.text(c.x + 22, c.y + 18, "S", theme::warning);
        if (code == primary_) ui_.frame(c, theme::accent, 2);
        else if (code == secondary_) ui_.frame(c, theme::warning, 2);
        if (ui_.hover(c)) {
            ui_.frame(c, theme::text);
            ui_.tooltip(fmt("%s — %s (код %d)", tileInfo(code).name, tileInfo(code).hint, code));
            if (ui_.pressed[0]) { primary_ = code; ui_.captured = true; }
            if (ui_.pressed[2]) { secondary_ = code; ui_.captured = true; }
        }
    }
    y += ((T_COUNT + cols - 1) / cols) * cell + 4;
    auto show = [&](int code, Color c, const char* who) {
        SDL_Rect src{TileGfx::imageFor(code) * kTilePx, 0, kTilePx, kTilePx}, dst{r.x + 8, y, 16, 16};
        SDL_RenderCopy(ren_, tiles_, &src, &dst);
        ui_.text(r.x + 28, y, who, c);
        ui_.textClipped({r.x + 68, y, r.w - 76, 16}, tileInfo(code).name);
        y += 18;
    };
    show(primary_, theme::accent, "ЛКМ");
    show(secondary_, theme::warning, "ПКМ");
    y += 6;
    ui_.line(r.x + 8, y, r.x + r.w - 8, y, theme::line);
    y += 6;
}

void App::drawProperties(Rect r, int& y) {
    Level& l = lvl();
    ui_.text(r.x + 8, y, fmt("Уровень %03d", cur_ + 1), theme::dim);
    y += 20;
    ui_.text(r.x + 8, y + 3, "Название");
    if (ui_.textField(2, {r.x + 80, y, r.w - 88, 22}, titleEdit_, kTitleLen, titleFilter,
                      "Название уровня (латиница, цифры, знаки; 23 символа)")) {
        beginEdit();
        l.setTitle(titleEdit_);
        endEdit();
    }
    y += 28;
    bool g = l.gravity();
    if (ui_.checkbox({r.x + 8, y, 120, 20}, "Гравитация", g, "Murphy падает вниз, если под ним пусто")) {
        beginEdit(); l.setGravity(g); endEdit(g ? "Гравитация включена" : "Гравитация выключена");
    }
    y += 24;
    ui_.text(r.x + 8, y + 2, "Зонки");
    static const char* fz[] = {"обычно", "1", "стоят"};
    static const char* fzTip[] = {"Зонки и инфотроны падают как обычно (0)",
                                  "Значение 1: зонки по «замороженной» ветке, инфотроны не падают",
                                  "Значение 2: зонки и инфотроны заморожены полностью"};
    int fzv = l.freezeZonks();
    for (int i = 0; i < 3; i++) {
        Rect b{r.x + 64 + i * 66, y, 62, 22};
        if (ui_.button(b, fz[i], true, fzv == i, fzTip[i])) { beginEdit(); l.setFreezeZonks(i); endEdit(); }
    }
    if (fzv > 2) ui_.text(r.x + 64, y + 24, fmt("(в файле %d — действует как 0)", fzv), theme::dim), y += 16;
    y += 28;
    int inf = l.count(T_INFOTRON), el = l.count(T_ELECTRON);
    ui_.text(r.x + 8, y + 3, "Собрать");
    int need = l.infotronsNeeded();
    if (ui_.button({r.x + 80, y, 24, 22}, "−", need > 0)) { beginEdit(); l.setInfotronsNeeded(need - 1); endEdit(); }
    ui_.fill({r.x + 106, y, 70, 22}, theme::panel2);
    ui_.textCentered({r.x + 106, y, 70, 22}, need ? fmt("%d", need) : fmt("все %d", inf));
    if (ui_.button({r.x + 178, y, 24, 22}, "+", need < 255)) {
        beginEdit(); l.setInfotronsNeeded(need == 0 ? std::min(255, inf) : need + 1); endEdit();
    }
    if (ui_.hover({r.x + 8, y, 200, 22})) ui_.tooltip("Сколько инфотронов нужно собрать (0 — все, что есть на уровне)");
    y += 26;
    ui_.text(r.x + 8, y, fmt("Инфотронов %d, электронов %d", inf, el), theme::dim);
    y += 18;
    ui_.text(r.x + 8, y, fmt("Зонков %d, сник-снаков %d, багов %d", l.count(T_ZONK), l.count(T_SNIKSNAK), l.count(T_BUG)),
             theme::dim);
    y += 22;
    ui_.line(r.x + 8, y, r.x + r.w - 8, y, theme::line);
    y += 6;
}

void App::drawPorts(Rect r, int& y) {
    Level& l = lvl();
    auto all = l.specialPorts();
    // records on special port cells; the others are junk from the disk
    std::vector<int> shown;
    for (size_t i = 0; i < all.size(); i++)
        if (all[i].cell < kMapW * kMapH && isSpecialPort(l.b[size_t(all[i].cell)])) shown.push_back(int(i));
    int junk = int(all.size() - shown.size());
    ui_.text(r.x + 8, y, fmt("Особые порты %d / %d", int(shown.size()), kMaxSpecialPorts), theme::dim);
    if (junk) {
        Rect jr{r.x + 170, y, r.w - 178, 16};
        ui_.text(jr.x, y, fmt("мусор: %d", junk), theme::warning);
        if (ui_.hover(jr)) ui_.tooltip("Записи портов не на особых портах (так на дискете) — уберутся при сохранении правленого уровня");
    }
    y += 20;
    if (shown.empty()) {
        ui_.text(r.x + 8, y, "нет (тайлы с буквой S)", theme::dim);
        return;
    }
    auto& ports = all;
    ui_.text(r.x + 8, y, "клетка", theme::dim);
    ui_.text(r.x + 92, y, "Грав.", theme::dim);
    ui_.text(r.x + 142, y, "Зонки", theme::dim);
    ui_.text(r.x + 200, y, "Враги", theme::dim);
    y += 18;
    int rows = std::max(1, (r.y + r.h - y) / 22);
    int n = int(shown.size());
    portScroll_ = std::clamp(portScroll_, 0, std::max(0, n - rows));
    if (ui_.hover({r.x, y, r.w, rows * 22}) && ui_.wheelY) portScroll_ = std::clamp(portScroll_ - ui_.wheelY, 0, std::max(0, n - rows));
    bool changed = false;
    for (int k = 0; k < rows && portScroll_ + k < n; k++) {
        SpecialPort& p = ports[size_t(shown[size_t(portScroll_ + k)])];
        int px = p.cell % kMapW, py = p.cell / kMapW;
        Rect lab{r.x + 8, y, 80, 20};
        if (ui_.hover(lab)) { ui_.fill(lab, theme::panel2); ui_.tooltip("Показать на карте"); if (ui_.pressed[0]) { centreOn(px, py); ui_.captured = true; } }
        ui_.text(r.x + 10, y + 2, fmt("%d (%d,%d)", portScroll_ + k + 1, px, py));
        bool gv = p.gravity != 0, ev = p.freezeEnemies != 0;
        if (ui_.checkbox({r.x + 96, y, 30, 20}, "", gv, "Гравитация после прохода")) { p.gravity = gv ? 1 : 0; changed = true; }
        const char* zl = p.freezeZonks == 2 ? "стоят" : p.freezeZonks == 0 ? "обычн" : "1";
        if (ui_.button({r.x + 138, y, 54, 20}, zl, true, p.freezeZonks == 2,
                       "Зонки после прохода: обычно (0) / 1 / стоят (2) — щелчок меняет")) {
            p.freezeZonks = uint8_t(p.freezeZonks == 0 ? 2 : p.freezeZonks == 2 ? 1 : 0);
            changed = true;
        }
        if (ui_.checkbox({r.x + 206, y, 30, 20}, "", ev, "Заморозить врагов после прохода")) { p.freezeEnemies = ev ? 1 : 0; changed = true; }
        y += 22;
    }
    if (changed) { beginEdit(); l.setSpecialPorts(ports); endEdit(); }
}

void App::drawMinimap(Rect r) {
    ui_.fill(r, {20, 21, 25});
    int s = std::max(1, std::min(r.w / kMapW, r.h / kMapH));
    int ox = r.x + (r.w - kMapW * s) / 2, oy = r.y + (r.h - kMapH * s) / 2;
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++) {
            uint32_t c = gfx_.averageColor(TileGfx::imageFor(lvl().tile(x, y)));
            int code = lvl().tile(x, y);
            if (code == T_MURPHY) c = 0xFFFF4040;
            else if (code == T_EXIT) c = 0xFFFFFFFF;
            else if (code == T_INFOTRON) c = 0xFF40E040;
            ui_.fill({ox + x * s, oy + y * s, s, s}, {uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c), 255});
        }
    // the visible part of the map
    int z = kZooms[zoomIdx_];
    int mapW = vw_ - kLeftW - kRightW, mapH = vh_ - kToolbarH - kStatusH - kIssuesH;
    Rect v{ox + panX_ * s / z, oy + panY_ * s / z, mapW * s / z, mapH * s / z};
    ui_.setClip(&r);
    ui_.frame(v, theme::accent);
    ui_.setClip(nullptr);
    if (ui_.hover(r) && ui_.down[0]) {
        int cx = (ui_.mx - ox) / s, cy = (ui_.my - oy) / s;
        panX_ = cx * z - mapW / 2;
        panY_ = cy * z - mapH / 2;
        ui_.captured = true;
    }
    if (ui_.hover(r)) ui_.tooltip("Мини-карта: щёлкните, чтобы перейти");
}

void App::drawMap(Rect r) {
    ui_.setClip(&r);
    ui_.fill(r, {16, 17, 20});
    int z = kZooms[zoomIdx_];
    Level& l = lvl();
    int x0 = std::max(0, panX_ / z), y0 = std::max(0, panY_ / z);
    int x1 = std::min(kMapW - 1, (panX_ + r.w) / z), y1 = std::min(kMapH - 1, (panY_ + r.h) / z);
    auto cellRect = [&](int cx, int cy) { return Rect{r.x + cx * z - panX_, r.y + cy * z - panY_, z, z}; };
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++) {
            Rect c = cellRect(cx, cy);
            int code = l.tile(cx, cy);
            SDL_Rect src{TileGfx::imageFor(code) * kTilePx, 0, kTilePx, kTilePx}, dst{c.x, c.y, z, z};
            SDL_RenderCopy(ren_, tiles_, &src, &dst);
            if (code >= T_COUNT) { ui_.fill(c, {255, 0, 255, 160}); }
            if (isSpecialPort(code)) {
                bool hasRec = l.findPort(cy * kMapW + cx) >= 0;
                Color mc = hasRec ? theme::warning : theme::error;
                if (z >= 16) ui_.text(c.x + z - 9, c.y + z - 15, "S", mc);
                else ui_.fill({c.x + z - 4, c.y, 4, 4}, mc);
            }
        }
    // border problems
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++)
            if ((cx == 0 || cy == 0 || cx == kMapW - 1 || cy == kMapH - 1) && !isHardware(l.tile(cx, cy)))
                ui_.frame(cellRect(cx, cy), theme::error, 2);
    if (grid_ && z >= 8) {
        Color gc{0, 0, 0, 90};
        for (int cx = x0; cx <= x1 + 1; cx++) { int x = r.x + cx * z - panX_; ui_.line(x, r.y + y0 * z - panY_, x, r.y + (y1 + 1) * z - panY_, gc); }
        for (int cy = y0; cy <= y1 + 1; cy++) { int y = r.y + cy * z - panY_; ui_.line(r.x + x0 * z - panX_, y, r.x + (x1 + 1) * z - panX_, y, gc); }
    }
    // map outline
    ui_.frame({r.x - panX_ - 1, r.y - panY_ - 1, kMapW * z + 2, kMapH * z + 2}, theme::line);
    // start camera
    if (showCamera_ && l.murphyCell() >= 0) {
        bool valid = true;
        Camera c = l.effectiveCamera(&valid);
        if (!valid) c = l.recommendedCamera();
        Rect cr{r.x + c.x * z - panX_, r.y + c.y * z - panY_, kCamW * z, kCamH * z};
        ui_.frame(cr, valid ? Color{120, 200, 255, 200} : theme::error, 2);
        ui_.text(cr.x + 4, cr.y + 2, valid ? "стартовый экран" : "стартовый экран (будет пересчитан)",
                 valid ? Color{120, 200, 255} : theme::error);
    }
    // selection
    if (hasSel_ || selecting_) {
        int sx0 = std::min(selX0_, selX1_), sx1 = std::max(selX0_, selX1_), sy0 = std::min(selY0_, selY1_), sy1 = std::max(selY0_, selY1_);
        Rect sr{r.x + sx0 * z - panX_, r.y + sy0 * z - panY_, (sx1 - sx0 + 1) * z, (sy1 - sy0 + 1) * z};
        ui_.fill(sr, {86, 156, 255, 50});
        ui_.frame(sr, theme::accent, 2);
    }
    // previews
    int hx = hoverX_, hy = hoverY_;
    auto ghost = [&](int cx, int cy, int code) {
        if (!l.inside(cx, cy)) return;
        Rect c = cellRect(cx, cy);
        SDL_Rect src{TileGfx::imageFor(code) * kTilePx, 0, kTilePx, kTilePx}, dst{c.x, c.y, z, z};
        SDL_SetTextureAlphaMod(tiles_, 170);
        SDL_RenderCopy(ren_, tiles_, &src, &dst);
        SDL_SetTextureAlphaMod(tiles_, 255);
        ui_.frame(c, {255, 255, 255, 120});
    };
    if (pasting_ && hx >= 0) {
        for (int j = 0; j < clip_.h; j++)
            for (int i = 0; i < clip_.w; i++) ghost(hx + i, hy + j, clip_.t[size_t(j * clip_.w + i)]);
        ui_.frame({r.x + hx * z - panX_, r.y + hy * z - panY_, clip_.w * z, clip_.h * z}, theme::accent, 2);
    } else if (stroking_ && (tool_ == Tool::Line || tool_ == Tool::Rect || tool_ == Tool::FillRect) && hx >= 0) {
        if (tool_ == Tool::Line) {
            int ax = sx_, ay = sy_, bx = hx, by = hy;
            int dx = std::abs(bx - ax), dy = -std::abs(by - ay), stx = ax < bx ? 1 : -1, sty = ay < by ? 1 : -1, e = dx + dy;
            for (;;) {
                ghost(ax, ay, strokeCode_);
                if (ax == bx && ay == by) break;
                int e2 = 2 * e;
                if (e2 >= dy) { e += dy; ax += stx; }
                if (e2 <= dx) { e += dx; ay += sty; }
            }
        } else {
            int ax = std::min(sx_, hx), bx = std::max(sx_, hx), ay = std::min(sy_, hy), by = std::max(sy_, hy);
            for (int y = ay; y <= by; y++)
                for (int x = ax; x <= bx; x++)
                    if (tool_ == Tool::FillRect || x == ax || x == bx || y == ay || y == by) ghost(x, y, strokeCode_);
        }
    } else if (hx >= 0 && !ui_.modal) {
        ui_.frame(cellRect(hx, hy), {255, 255, 255, 200}, 2);
    }
    // flash (jump to an issue / port)
    if (flashX_ >= 0 && SDL_GetTicks() - flashTime_ < 1500) {
        if ((SDL_GetTicks() - flashTime_) / 250 % 2 == 0) ui_.frame(cellRect(flashX_, flashY_), theme::warning, 3);
    }
    ui_.setClip(nullptr);
}

void App::drawIssues(Rect r) {
    ui_.fill(r, theme::panel);
    ui_.line(r.x, r.y, r.x + r.w, r.y, theme::line);
    const auto& is = issues();
    int errs = 0, warns = 0;
    for (const Issue& i : is) { errs += i.level == Issue::Error; warns += i.level == Issue::Warning; }
    int y = r.y + 4;
    if (is.empty()) {
        ui_.text(r.x + 8, y, "Проверка: ошибок нет — уровень готов к игре на Amiga", theme::ok);
        return;
    }
    ui_.text(r.x + 8, y, fmt("Проверка: ошибок %d, предупреждений %d (щелчок — показать клетку)", errs, warns),
             errs ? theme::error : theme::warning);
    y += 20;
    int rows = (r.y + r.h - y) / kRowH;
    if (ui_.hover(r) && ui_.wheelY) issueScroll_ -= ui_.wheelY;
    issueScroll_ = std::clamp(issueScroll_, 0, std::max(0, int(is.size()) - rows));
    for (int k = 0; k < rows && issueScroll_ + k < int(is.size()); k++) {
        const Issue& i = is[size_t(issueScroll_ + k)];
        Rect row{r.x + 4, y, r.w - 8, kRowH};
        if (i.x >= 0 && ui_.hover(row)) {
            ui_.fill(row, theme::panel2);
            if (ui_.pressed[0]) { centreOn(i.x, i.y); ui_.captured = true; }
        }
        const char* mark = i.level == Issue::Error ? "●" : i.level == Issue::Warning ? "▲" : "•";
        Color c = i.level == Issue::Error ? theme::error : i.level == Issue::Warning ? theme::warning : theme::info;
        ui_.text(row.x + 4, row.y + 1, i.level == Issue::Error ? "!" : i.level == Issue::Warning ? "▲" : "·", c);
        (void)mark;
        std::string t = i.text + (i.x >= 0 ? fmt("  (%d, %d)", i.x, i.y) : "");
        ui_.textClipped({row.x + 20, row.y, row.w - 24, row.h}, t);
        if (ui_.hover(row) && ui_.utf8Length(t) * 8 > size_t(row.w - 24)) ui_.tooltip(t);
        y += kRowH;
    }
}

void App::drawStatus(Rect r) {
    ui_.fill(r, theme::panel2);
    std::string left;
    if (hoverX_ >= 0) {
        int code = lvl().tile(hoverX_, hoverY_);
        left = fmt("x %2d  y %2d   %s (%d)", hoverX_, hoverY_, tileInfo(code).name, code);
        int k = lvl().findPort(hoverY_ * kMapW + hoverX_);
        if (k >= 0) {
            SpecialPort p = lvl().specialPorts()[size_t(k)];
            left += fmt("   порт %d: гравитация %s, зонки %d, враги %s", k + 1, p.gravity ? "вкл" : "выкл", p.freezeZonks,
                        p.freezeEnemies ? "стоят" : "ходят");
        }
    } else {
        left = fmt("Уровень %03d — %s", cur_ + 1, trimTitle(lvl().title()).c_str());
    }
    ui_.text(r.x + 8, r.y + 3, left);
    if (!status_.empty() && SDL_GetTicks() - statusTime_ < 8000) {
        int w = ui_.textWidth(status_);
        ui_.text(std::max(r.x + 8 + ui_.textWidth(left) + 24, r.x + r.w - w - 8), r.y + 3, status_, statusColor_);
    }
}

void App::drawModal() {
    ui_.modal = false;
    ui_.fill({0, 0, vw_, vh_}, {0, 0, 0, 140});
    std::vector<std::string> lines;
    std::stringstream ss(modal_.text);
    std::string ln;
    int w = std::max(360, ui_.textWidth(modal_.title) + 40);
    while (std::getline(ss, ln)) { lines.push_back(ln); w = std::max(w, ui_.textWidth(ln) + 40); }
    int h = 70 + int(lines.size()) * 18 + 40;
    Rect box{(vw_ - w) / 2, (vh_ - h) / 2, w, h};
    ui_.fill(box, theme::panel);
    ui_.frame(box, theme::accent);
    ui_.text(box.x + 20, box.y + 14, modal_.title, theme::accent);
    int y = box.y + 44;
    for (const std::string& l : lines) { ui_.text(box.x + 20, y, l); y += 18; }
    int bw = 130, bx = box.x + box.w - 20 - int(modal_.buttons.size()) * (bw + 8) + 8;
    int chosen = -1;
    for (size_t i = 0; i < modal_.buttons.size(); i++)
        if (ui_.button({bx + int(i) * (bw + 8), box.y + box.h - 40, bw, 26}, modal_.buttons[i], true, i == 0)) chosen = int(i);
    for (const SDL_Keysym& k : ui_.keys) {
        if (k.sym == SDLK_RETURN || k.sym == SDLK_KP_ENTER) chosen = 0;
        if (k.sym == SDLK_ESCAPE) chosen = int(modal_.buttons.size()) - 1;
    }
    if (chosen >= 0) {
        modalOn_ = false;
        auto done = modal_.done;
        if (done) done(chosen);
    }
    ui_.modal = true;
}

void App::drawHelp() {
    static const char* text[] = {
        "Мышь",
        "  ЛКМ — рисовать основным тайлом, ПКМ — вторым (по умолчанию «пусто»)",
        "  Alt+ЛКМ / Alt+ПКМ или средняя кнопка — взять тайл с карты",
        "  Колесо — прокрутка, Shift+колесо — вбок, Ctrl+колесо — масштаб",
        "  Пробел+ЛКМ или средняя кнопка с перетаскиванием — двигать карту",
        "  Перетащите в окно файл .SP (уровень) или LEVELS.DAT (все 111) — импорт",
        "Инструменты",
        "  P карандаш   L линия   R рамка   F прямоугольник   G заливка   S выделение",
        "  0–9 — быстрый выбор тайла по коду (0 пусто, 1 зонк, 2 база, 3 Murphy, 4 инфотрон …)",
        "Выделение и буфер",
        "  Ctrl+C / Ctrl+X — копировать / вырезать, Ctrl+V — вставлять (щелчком, Esc — хватит)",
        "  X / Y при вставке — отразить; Shift+H / Shift+V — отразить выделенное; Del — очистить",
        "  Ctrl+A — выделить весь уровень",
        "Уровни и файлы",
        "  PageUp / PageDown — предыдущий / следующий уровень, Alt+↑/↓ — переставить уровень",
        "  Ctrl+Shift+C / Ctrl+Shift+V — скопировать / вставить весь уровень",
        "  Ctrl+S — сохранить (прежний файл — LEVELS.DAT.bak), Ctrl+E — экспорт .SP, Ctrl+P — картинка",
        "  F5 — сыграть уровень в игре; Ctrl+Z / Ctrl+Y — отменить / вернуть",
        "Вид",
        "  Ctrl+G — сетка, C — стартовый экран игры, Ctrl+0 — весь уровень, стрелки — прокрутка",
        "Правила Amiga, которые редактор соблюдает сам",
        "  • камера старта ставится на Murphy (PC-редакторы пишут нули — камера уезжает за карту)",
        "  • записи особых портов следуют за тайлами (S), не больше 10; лишние байты обнуляются",
        "  • рамка должна быть из «железа»: объект, ушедший за карту, вешает игру на Amiga",
        "  • неизменённые уровни сохраняются байт в байт",
        "",
        "F1 или Esc — закрыть справку",
    };
    ui_.modal = false;
    ui_.fill({0, 0, vw_, vh_}, {0, 0, 0, 150});
    int n = int(sizeof text / sizeof text[0]);
    int w = 820, h = n * 18 + 40;
    Rect box{(vw_ - w) / 2, std::max(10, (vh_ - h) / 2), w, h};
    ui_.fill(box, theme::panel);
    ui_.frame(box, theme::accent);
    int y = box.y + 20;
    for (const char* t : text) {
        bool head = t[0] && t[0] != ' ';
        ui_.text(box.x + 20, y, t, head ? theme::accent : theme::text);
        y += 18;
    }
    if (!helpOpened_) {  // not the key press that opened it
        for (const SDL_Keysym& k : ui_.keys)
            if (k.sym == SDLK_F1 || k.sym == SDLK_ESCAPE) help_ = false;
        if (ui_.pressed[0] && !box.contains(ui_.mx, ui_.my)) help_ = false;
    }
    helpOpened_ = false;
    ui_.modal = true;
}

// ---------------------------------------------------------------------------
// input

void App::handleMapInput(Rect r) {
    int lx = ui_.mx - r.x, ly = ui_.my - r.y;
    bool over = r.contains(ui_.mx, ui_.my) && !ui_.captured;
    int cx = -1, cy = -1;
    if (over && cellAt(lx, ly, cx, cy)) { hoverX_ = cx; hoverY_ = cy; }
    else { hoverX_ = hoverY_ = -1; }
    const Uint8* kb = SDL_GetKeyboardState(nullptr);
    bool ctrl = kb[SDL_SCANCODE_LCTRL] || kb[SDL_SCANCODE_RCTRL];
    bool alt = kb[SDL_SCANCODE_LALT] || kb[SDL_SCANCODE_RALT];
    bool shift = kb[SDL_SCANCODE_LSHIFT] || kb[SDL_SCANCODE_RSHIFT];
    bool space = kb[SDL_SCANCODE_SPACE] && ui_.focus == 0;

    // wheel
    if (over && ui_.wheelY) {
        if (ctrl) zoomAt(zoomIdx_ + (ui_.wheelY > 0 ? 1 : -1), lx, ly);
        else if (shift) panX_ -= ui_.wheelY * kZooms[zoomIdx_] * 3;
        else panY_ -= ui_.wheelY * kZooms[zoomIdx_] * 3;
    }
    if (over && ui_.wheelX) panX_ += ui_.wheelX * kZooms[zoomIdx_] * 3;

    // panning
    if (over && (ui_.pressed[1] || (space && ui_.pressed[0]))) {
        if (ui_.pressed[1] && hoverX_ >= 0 && !space) {
            // middle click without moving picks the tile; dragging pans
        }
        panning_ = true;
        panStartX_ = ui_.mx; panStartY_ = ui_.my; panOrigX_ = panX_; panOrigY_ = panY_;
    }
    if (panning_) {
        panX_ = panOrigX_ - (ui_.mx - panStartX_);
        panY_ = panOrigY_ - (ui_.my - panStartY_);
        if (!ui_.down[1] && !ui_.down[0]) {
            if (std::abs(ui_.mx - panStartX_) < 3 && std::abs(ui_.my - panStartY_) < 3 && hoverX_ >= 0 && !space) {
                primary_ = lvl().tile(hoverX_, hoverY_);
                message(std::string("Основной тайл: ") + tileInfo(primary_).name);
            }
            panning_ = false;
        }
        return;
    }

    // keep the map reachable
    int z = kZooms[zoomIdx_];
    panX_ = std::clamp(panX_, -r.w + z * 2, kMapW * z - z * 2);
    panY_ = std::clamp(panY_, -r.h + z * 2, kMapH * z - z * 2);

    // eyedropper
    if (over && alt && hoverX_ >= 0 && (ui_.pressed[0] || ui_.pressed[2])) {
        int code = lvl().tile(hoverX_, hoverY_);
        if (ui_.pressed[0]) primary_ = code; else secondary_ = code;
        message(std::string(ui_.pressed[0] ? "Основной тайл: " : "Второй тайл: ") + tileInfo(code).name);
        return;
    }

    // paste mode
    if (pasting_) {
        if (over && hoverX_ >= 0 && ui_.pressed[0]) pasteAt(hoverX_, hoverY_);
        if (over && ui_.pressed[2]) { pasting_ = false; message("Вставка закончена"); }
        return;
    }

    // tools
    if (!stroking_ && !selecting_ && over && hoverX_ >= 0 && (ui_.pressed[0] || ui_.pressed[2])) {
        int code = ui_.pressed[0] ? primary_ : secondary_;
        if (tool_ == Tool::Select) {
            if (ui_.pressed[2]) { hasSel_ = false; return; }
            selecting_ = true;
            hasSel_ = false;
            selX0_ = selX1_ = hoverX_;
            selY0_ = selY1_ = hoverY_;
            return;
        }
        if (tool_ == Tool::Flood) {
            beginEdit();
            flood(hoverX_, hoverY_, code);
            endEdit();
            return;
        }
        stroking_ = true;
        strokeCode_ = code;
        sx_ = lx_ = hoverX_;
        sy_ = ly_ = hoverY_;
        beginEdit();
        if (tool_ == Tool::Pencil) paint(hoverX_, hoverY_, code);
    }
    if (selecting_) {
        if (hoverX_ >= 0) { selX1_ = hoverX_; selY1_ = hoverY_; }
        if (!ui_.down[0]) { selecting_ = false; hasSel_ = true; }
        return;
    }
    if (stroking_) {
        if (tool_ == Tool::Pencil && hoverX_ >= 0 && (hoverX_ != lx_ || hoverY_ != ly_)) {
            paintLine(lx_, ly_, hoverX_, hoverY_, strokeCode_);
            lx_ = hoverX_;
            ly_ = hoverY_;
        }
        if (!ui_.down[0] && !ui_.down[2]) {
            int ex = hoverX_ >= 0 ? hoverX_ : lx_, ey = hoverY_ >= 0 ? hoverY_ : ly_;
            if (tool_ == Tool::Line) paintLine(sx_, sy_, ex, ey, strokeCode_);
            else if (tool_ == Tool::Rect) paintRect(sx_, sy_, ex, ey, strokeCode_, false);
            else if (tool_ == Tool::FillRect) paintRect(sx_, sy_, ex, ey, strokeCode_, true);
            stroking_ = false;
            endEdit();
        } else if (hoverX_ >= 0) {
            lx_ = hoverX_;
            ly_ = hoverY_;
        }
    }
}

void App::handleKeys() {
    if (ui_.focus) {
        // Esc / Enter inside a text field only leave it
        return;
    }
    for (const SDL_Keysym& k : ui_.keys) {
        bool ctrl = k.mod & KMOD_CTRL, shift = k.mod & KMOD_SHIFT, alt = k.mod & KMOD_ALT;
        switch (k.sym) {
        case SDLK_s: if (ctrl) { save(); continue; } break;
        case SDLK_z: if (ctrl) { shift ? redo() : undo(); continue; } break;
        case SDLK_y: if (ctrl) { redo(); continue; } break;
        case SDLK_c:
            if (ctrl && shift) { levelClip_ = lvl(); haveLevelClip_ = true; message(fmt("Уровень %03d скопирован", cur_ + 1)); continue; }
            if (ctrl) { copySelection(false); continue; }
            if (!alt) { showCamera_ = !showCamera_; continue; }
            break;
        case SDLK_x:
            if (ctrl) { copySelection(true); continue; }
            if (pasting_) { mirrorClip(true); continue; }
            break;
        case SDLK_v:
            if (ctrl && shift) { if (haveLevelClip_) replaceLevel(cur_, levelClip_, "Уровень вставлен (Ctrl+Z — отменить)"); continue; }
            if (ctrl) {
                if (clip_.w) { pasting_ = true; hasSel_ = false; message("Вставка: щелчок — поставить, X/Y — отразить, Esc — закончить"); }
                else message("Буфер пуст: выделите область и нажмите Ctrl+C");
                continue;
            }
            if (shift) { mirrorSelection(false); continue; }
            break;
        case SDLK_h: if (shift) { mirrorSelection(true); continue; } break;
        case SDLK_a: if (ctrl) { tool_ = Tool::Select; hasSel_ = true; selX0_ = selY0_ = 0; selX1_ = kMapW - 1; selY1_ = kMapH - 1; continue; } break;
        case SDLK_g: if (ctrl) { grid_ = !grid_; continue; } break;
        case SDLK_e: if (ctrl) { exportLevel(); continue; } break;
        case SDLK_p: if (ctrl) { exportPicture(); continue; } break;
        case SDLK_0: case SDLK_KP_0: if (ctrl) { fitMap(); continue; } break;
        case SDLK_EQUALS: case SDLK_KP_PLUS: if (ctrl) { zoomAt(zoomIdx_ + 1, (vw_ - kLeftW - kRightW) / 2, 200); continue; } break;
        case SDLK_MINUS: case SDLK_KP_MINUS: if (ctrl) { zoomAt(zoomIdx_ - 1, (vw_ - kLeftW - kRightW) / 2, 200); continue; } break;
        default: break;
        }
        if (ctrl) continue;
        switch (k.sym) {
        case SDLK_F1: help_ = true; helpOpened_ = true; break;
        case SDLK_F5: startTest(); break;
        case SDLK_ESCAPE: pasting_ = false; hasSel_ = false; selecting_ = false; break;
        case SDLK_DELETE: case SDLK_BACKSPACE: deleteSelection(); break;
        case SDLK_PAGEUP: selectLevel(cur_ - 1); break;
        case SDLK_PAGEDOWN: selectLevel(cur_ + 1); break;
        case SDLK_HOME: if (!alt) selectLevel(0); break;
        case SDLK_END: if (!alt) selectLevel(kLevelCount - 1); break;
        case SDLK_UP: if (alt) swapLevels(cur_, cur_ - 1); else panY_ -= kZooms[zoomIdx_] * 2; break;
        case SDLK_DOWN: if (alt) swapLevels(cur_, cur_ + 1); else panY_ += kZooms[zoomIdx_] * 2; break;
        case SDLK_LEFT: panX_ -= kZooms[zoomIdx_] * 2; break;
        case SDLK_RIGHT: panX_ += kZooms[zoomIdx_] * 2; break;
        case SDLK_y: if (pasting_) mirrorClip(false); break;
        default:
            if (k.sym >= SDLK_0 && k.sym <= SDLK_9 && !shift) { primary_ = int(k.sym - SDLK_0); message(std::string("Основной тайл: ") + tileInfo(primary_).name); break; }
            if (shift) break;
            for (const ToolInfo& t : kTools)
                if (k.sym == t.sym) { tool_ = t.t; pasting_ = false; message(std::string("Инструмент: ") + t.name); }
            break;
        }
    }
}

}  // namespace editor

int main(int argc, char** argv) {
    editor::App app;
    return app.run(argc, argv);
}
