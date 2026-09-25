#include "level.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "i18n.hpp"

namespace editor {

namespace {

const TileInfo kTiles[T_COUNT] = {
    {"Empty", "empty cell", "Пусто", "пустая клетка"},
    {"Zonk", "boulder: falls and rolls off", "Зонк", "камень: падает и скатывается"},
    {"Base", "Murphy eats it", "База", "съедается Murphy"},
    {"Murphy", "the player (exactly one per level)", "Murphy", "игрок (ровно один на уровень)"},
    {"Infotron", "collect the required number", "Инфотрон", "собрать нужное число"},
    {"RAM chip", "wall, destroyed by explosions", "RAM-чип", "стена, разрушается взрывом"},
    {"Hardware", "indestructible wall", "Железо", "неразрушимая стена"},
    {"Exit", "opens when the infotrons are collected", "Выход", "открывается после сбора инфотронов"},
    {"Orange disk", "falls, explodes on impact", "Оранжевый диск", "падает, взрывается при ударе"},
    {"Port →", "pass to the right only", "Порт →", "проход только вправо"},
    {"Port ↓", "pass downwards only", "Порт ↓", "проход только вниз"},
    {"Port ←", "pass to the left only", "Порт ←", "проход только влево"},
    {"Port ↑", "pass upwards only", "Порт ↑", "проход только вверх"},
    {"Special port →", "right; changes gravity / freezes", "Особый порт →", "вправо; меняет гравитацию/заморозки"},
    {"Special port ↓", "down; changes gravity / freezes", "Особый порт ↓", "вниз; меняет гравитацию/заморозки"},
    {"Special port ←", "left; changes gravity / freezes", "Особый порт ←", "влево; меняет гравитацию/заморозки"},
    {"Special port ↑", "up; changes gravity / freezes", "Особый порт ↑", "вверх; меняет гравитацию/заморозки"},
    {"Snik snak", "enemy, follows the left wall", "Сник-снак", "враг, ходит вдоль левой стены"},
    {"Yellow disk", "explodes when a terminal is used", "Жёлтый диск", "взрывается от терминала"},
    {"Terminal", "blows up all yellow disks", "Терминал", "взрывает все жёлтые диски"},
    {"Red disk", "Murphy can pick it up and drop it", "Красный диск", "Murphy может заложить"},
    {"Port ↕", "pass up and down", "Порт ↕", "проход вверх и вниз"},
    {"Port ↔", "pass left and right", "Порт ↔", "проход влево и вправо"},
    {"Port ✚", "pass in all directions", "Порт ✚", "проход во все стороны"},
    {"Electron", "enemy; leaves 9 infotrons when it explodes", "Электрон", "враг; при взрыве — 9 инфотронов"},
    {"Bug", "base that gives electric shocks", "Баг", "база, которая бьёт током"},
    {"RAM chip (left)", "left half of a chip", "RAM-чип (лев.)", "левая половина чипа"},
    {"RAM chip (right)", "right half of a chip", "RAM-чип (прав.)", "правая половина чипа"},
    {"Hardware 1", "decorative wall variant", "Железо 1", "вариант оформления стены"},
    {"Hardware 2", "decorative wall variant", "Железо 2", "вариант оформления стены"},
    {"Hardware 3", "decorative wall variant", "Железо 3", "вариант оформления стены"},
    {"Hardware 4", "decorative wall variant", "Железо 4", "вариант оформления стены"},
    {"Hardware 5", "decorative wall variant", "Железо 5", "вариант оформления стены"},
    {"Hardware 6", "decorative wall variant", "Железо 6", "вариант оформления стены"},
    {"Hardware 7", "decorative wall variant", "Железо 7", "вариант оформления стены"},
    {"Hardware 8", "decorative wall variant", "Железо 8", "вариант оформления стены"},
    {"Hardware 9", "decorative wall variant", "Железо 9", "вариант оформления стены"},
    {"Hardware 10", "decorative wall variant", "Железо 10", "вариант оформления стены"},
    {"RAM chip (top)", "upper half of a chip", "RAM-чип (верх)", "верхняя половина чипа"},
    {"RAM chip (bottom)", "lower half of a chip", "RAM-чип (низ)", "нижняя половина чипа"},
};

const TileInfo kUnknown = {"Unknown code", "the game does not know this cell code", "Неизвестный код",
                           "игра не знает этот код клетки"};

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

}  // namespace

const char* TileInfo::name() const { return tr(nameEn, nameRu); }
const char* TileInfo::hint() const { return tr(hintEn, hintRu); }

const TileInfo& tileInfo(int code) { return code >= 0 && code < T_COUNT ? kTiles[code] : kUnknown; }
bool isHardware(int c) { return c == T_HARDWARE || (c >= T_HW_FIRST && c <= T_HW_LAST); }
bool isSpecialPort(int c) { return c >= T_SPORT_RIGHT && c <= T_SPORT_UP; }

int mirrorTileH(int c) {
    switch (c) {
    case T_PORT_RIGHT: return T_PORT_LEFT;
    case T_PORT_LEFT: return T_PORT_RIGHT;
    case T_SPORT_RIGHT: return T_SPORT_LEFT;
    case T_SPORT_LEFT: return T_SPORT_RIGHT;
    case T_RAM_LEFT: return T_RAM_RIGHT;
    case T_RAM_RIGHT: return T_RAM_LEFT;
    default: return c;
    }
}

int mirrorTileV(int c) {
    switch (c) {
    case T_PORT_UP: return T_PORT_DOWN;
    case T_PORT_DOWN: return T_PORT_UP;
    case T_SPORT_UP: return T_SPORT_DOWN;
    case T_SPORT_DOWN: return T_SPORT_UP;
    case T_RAM_TOP: return T_RAM_BOTTOM;
    case T_RAM_BOTTOM: return T_RAM_TOP;
    default: return c;
    }
}

Level Level::blank() {
    Level l;
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++)
            l.setTile(x, y, (x == 0 || y == 0 || x == kMapW - 1 || y == kMapH - 1) ? T_HARDWARE : T_BASE);
    l.setTile(3, 2, T_MURPHY);
    l.setTile(kMapW - 4, kMapH - 3, T_EXIT);
    l.setTitle("------ NEW LEVEL ------");
    l.normalize();
    return l;
}

std::string Level::title() const { return std::string(reinterpret_cast<const char*>(&b[1446]), kTitleLen); }

void Level::setTitle(const std::string& t) {
    for (int i = 0; i < kTitleLen; i++) {
        char c = i < int(t.size()) ? t[size_t(i)] : ' ';
        if (c >= 'a' && c <= 'z') c = char(c - 32);
        if (uint8_t(c) < 32 || uint8_t(c) > 95) c = ' ';
        b[size_t(1446 + i)] = uint8_t(c);
    }
}

Camera Level::storedCamera() const { return {b[1440] << 8 | b[1441], b[1442] << 8 | b[1443]}; }

void Level::setStoredCamera(Camera c) {
    b[1440] = uint8_t(c.x >> 8); b[1441] = uint8_t(c.x);
    b[1442] = uint8_t(c.y >> 8); b[1443] = uint8_t(c.y);
}

Camera Level::recommendedCamera() const {
    int m = murphyCell();
    if (m < 0) return {0, 0};
    int mx = m % kMapW, my = m / kMapW;
    Camera c;
    c.x = clampi(mx - 10, 0, 39);
    c.y = clampi(my - 6, 0, 11);
    return c;
}

// init_scroll_position ($8D1C)
Camera Level::effectiveCamera(bool* valid) const {
    Camera s = storedCamera();
    int vx = s.x == 40 ? 39 : s.x, vy = s.y == 12 ? 11 : s.y;
    int m = murphyCell();
    bool ok = true;
    if (m < 0) m = 0;
    int mx = m % kMapW, my = m / kMapW;
    if (my < vy) ok = false;  // divu of a negative number: result undefined
    int sy = my - vy, sx = mx - vx;
    auto centreY = [&] { int d = sy - 6; vy += d; sy -= d; };
    auto centreX = [&] { int d = sx - 10; vx += d; sx -= d; };
    if (vy == 0) { if (sy > 6) centreY(); }
    else if (vy == 11) { if (sy < 6) centreY(); }
    else centreY();
    if (vx == 0) { if (sx > 11) centreX(); }
    else if (vx == 39) { if (sx < 10) centreX(); }
    else centreX();
    if (vx < 0 || vx > 39 || vy < 0 || vy > 11) ok = false;
    if (valid) *valid = ok;
    return {vx, vy};
}

std::vector<SpecialPort> Level::specialPorts() const {
    std::vector<SpecialPort> v;
    for (int i = 0; i < kMaxSpecialPorts; i++) {
        const uint8_t* e = &b[size_t(1472 + i * 6)];
        int off = e[0] << 8 | e[1];
        if (off == 0) continue;
        SpecialPort p;
        p.cell = off / 2;
        p.gravity = e[2];
        p.freezeZonks = e[3];
        p.freezeEnemies = e[4];
        v.push_back(p);
    }
    return v;
}

void Level::setSpecialPorts(const std::vector<SpecialPort>& ports) {
    for (int i = 1472; i < 1536; i++) b[size_t(i)] = 0;
    int n = 0;
    for (const SpecialPort& p : ports) {
        if (n >= kMaxSpecialPorts) break;
        uint8_t* e = &b[size_t(1472 + n * 6)];
        int off = p.cell * 2;
        e[0] = uint8_t(off >> 8); e[1] = uint8_t(off);
        e[2] = p.gravity; e[3] = p.freezeZonks; e[4] = p.freezeEnemies; e[5] = 0;
        n++;
    }
    b[1471] = uint8_t(n);
}

int Level::findPort(int cell) const {
    auto v = specialPorts();
    for (size_t i = 0; i < v.size(); i++)
        if (v[i].cell == cell) return int(i);
    return -1;
}

int Level::count(int code) const {
    int n = 0;
    for (int i = 0; i < kMapW * kMapH; i++) n += b[size_t(i)] == code;
    return n;
}

int Level::murphyCell() const {
    for (int i = 0; i < kMapW * kMapH; i++)
        if (b[size_t(i)] == T_MURPHY) return i;
    return -1;
}

void Level::fixCamera() {
    bool valid = false;
    Camera eff = effectiveCamera(&valid);
    setStoredCamera(valid ? eff : recommendedCamera());
}

void Level::normalize(bool autoCamera) {
    // keep the camera the game would show; recentre only if it leaves the map
    if (autoCamera) fixCamera();
    std::vector<SpecialPort> keep;
    for (const SpecialPort& p : specialPorts())
        if (p.cell < kMapW * kMapH && isSpecialPort(b[size_t(p.cell)]) &&
            std::none_of(keep.begin(), keep.end(), [&](const SpecialPort& k) { return k.cell == p.cell; }))
            keep.push_back(p);
    setSpecialPorts(keep);
    b[1445] = 0;
}

std::vector<Issue> Level::validate() const {
    std::vector<Issue> out;
    auto add = [&](Issue::Level lv, std::string t, int cell = -1) {
        out.push_back({lv, std::move(t), cell < 0 ? -1 : cell % kMapW, cell < 0 ? -1 : cell / kMapW});
    };
    char buf[256];
    int murphys = count(T_MURPHY);
    if (murphys == 0) add(Issue::Error, tr("No Murphy: the level will not start", "Нет Murphy — уровень не запустится"));
    if (murphys > 1) {
        int second = -1, n = 0;
        for (int i = 0; i < kMapW * kMapH; i++)
            if (b[size_t(i)] == T_MURPHY && ++n == 2) { second = i; break; }
        std::snprintf(buf, sizeof buf, tr("Murphy is on the level %d times: the game controls the first one, the others are still \"doubles\"",
                                         "Murphy на уровне %d раз: игра управляет первым, остальные — неподвижные «двойники»"), murphys);
        add(Issue::Warning, buf, second);
    }
    if (count(T_EXIT) == 0) add(Issue::Warning, tr("No exit: the level cannot be finished", "Нет выхода — уровень нельзя пройти"));

    int infotrons = count(T_INFOTRON), electrons = count(T_ELECTRON);
    int need = infotronsNeeded() ? infotronsNeeded() : infotrons;
    if (infotronsNeeded() > infotrons + 9 * electrons) {
        std::snprintf(buf, sizeof buf, tr("%d infotrons needed, the level has %d (and %d electrons × 9)",
                                         "Нужно %d инфотронов, а на уровне %d (и %d электронов × 9)"), infotronsNeeded(),
                      infotrons, electrons);
        add(Issue::Error, buf);
    } else if (infotronsNeeded() > infotrons) {
        std::snprintf(buf, sizeof buf, tr("%d infotrons needed, the level has only %d: the rest must come from electrons",
                                         "Нужно %d инфотронов, на уровне только %d — остальные придётся добыть из электронов"),
                      infotronsNeeded(), infotrons);
        add(Issue::Warning, buf);
    }
    (void)need;

    // border
    int holes = 0, firstHole = -1;
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++) {
            if (x != 0 && y != 0 && x != kMapW - 1 && y != kMapH - 1) continue;
            if (!isHardware(tile(x, y))) { if (holes++ == 0) firstHole = y * kMapW + x; }
        }
    if (holes) {
        std::snprintf(buf, sizeof buf,
                      tr("Border is not hardware in %d cells: Murphy cannot pass the edge, but a zonk or an enemy "
                         "leaving the map hangs the game on the Amiga",
                         "Рамка не из «железа» в %d клетках: Murphy через край не пройдёт, но зонк или враг, "
                         "ушедший за карту, вешает игру на Amiga"), holes);
        add(Issue::Error, buf, firstHole);
    }

    // special ports
    int sports = 0, missing = -1;
    for (int i = 0; i < kMapW * kMapH; i++)
        if (isSpecialPort(b[size_t(i)])) {
            sports++;
            if (findPort(i) < 0 && missing < 0) missing = i;
        }
    if (sports > kMaxSpecialPorts) {
        std::snprintf(buf, sizeof buf, tr("%d special ports, the level format holds %d", "Особых портов %d, а в формате уровня помещается %d"),
                      sports, kMaxSpecialPorts);
        add(Issue::Error, buf);
    }
    if (missing >= 0) add(Issue::Warning, tr("Special port without a record: works as a plain port", "Особый порт без записи: работает как обычный порт"), missing);
    for (const SpecialPort& p : specialPorts()) {
        if (p.cell >= kMapW * kMapH || !isSpecialPort(b[size_t(p.cell)])) {
            add(Issue::Info, tr("Special port record not on a special port (junk): removed when saved",
                                "Запись особого порта не на особом порту (мусор) — уберётся при сохранении"),
                p.cell < kMapW * kMapH ? p.cell : -1);
            break;
        }
    }
    if (b[1532] || b[1533] || b[1534] || b[1535])
        add(Issue::Info, tr("Bytes 1532–1535 are not zero (the game reads them as an 11th port): cleared when saved",
                            "Байты 1532–1535 не нулевые (игра читает их как 11-й порт) — обнулятся при сохранении"));

    bool valid = true;
    Camera eff = effectiveCamera(&valid);
    if (!valid) {
        std::snprintf(buf, sizeof buf,
                      tr("Start camera leaves the map (%d, %d): Murphy's step is limited by the screen, not by the map, so he can walk "
                         "off the level",
                         "Стартовая камера уходит за карту (%d, %d): шаг Murphy ограничен экраном, а не картой — он сможет уйти за "
                         "уровень"),
                      eff.x, eff.y);
        add(Issue::Error, buf);
    }

    std::string t = title();
    for (char c : t)
        if (uint8_t(c) < 32 || uint8_t(c) > 95) { add(Issue::Warning, tr("The title has characters the game font does not have", "В названии есть символы, которых нет в шрифте игры")); break; }
    for (int i = 0; i < kMapW * kMapH; i++)
        if (b[size_t(i)] >= T_COUNT) { add(Issue::Error, tr("Unknown cell code", "Неизвестный код клетки"), i); break; }
    return out;
}

bool LevelSet::readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool LevelSet::writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(f);
}

bool LevelSet::load(const std::string& path, std::string* err) {
    std::vector<uint8_t> d;
    if (!readFile(path, d)) { if (err) *err = tr("cannot read ", "не удалось прочитать ") + path; return false; }
    if (d.size() != size_t(kLevelCount * kLevelSize)) {
        if (err) *err = path + tr(": 111 levels of 1536 bytes expected", ": ожидалось 111 уровней по 1536 байт");
        return false;
    }
    for (int i = 0; i < kLevelCount; i++)
        std::copy_n(d.begin() + std::ptrdiff_t(i * kLevelSize), kLevelSize, levels[size_t(i)].b.begin());
    edited.fill(false);
    return true;
}

bool LevelSet::save(const std::string& path, std::string* err, bool autoCamera) {
    std::vector<uint8_t> d;
    d.reserve(size_t(kLevelCount * kLevelSize));
    for (int i = 0; i < kLevelCount; i++) {
        if (edited[size_t(i)]) levels[size_t(i)].normalize(autoCamera);
        d.insert(d.end(), levels[size_t(i)].b.begin(), levels[size_t(i)].b.end());
    }
    // write to a temporary file first, then replace
    std::string tmp = path + ".tmp";
    if (!writeFile(tmp, d)) { if (err) *err = tr("cannot write ", "не удалось записать ") + tmp; return false; }
    std::error_code ec;
    std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path), ec);
    if (ec) {
        std::filesystem::remove(std::filesystem::u8path(path), ec);
        std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path), ec);
        if (ec) { if (err) *err = tr("cannot replace ", "не удалось заменить ") + path; return false; }
    }
    return true;
}

}  // namespace editor
